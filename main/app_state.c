#include "app_state.h"
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "bt_keymap.h"
#include "esp_random.h"

static app_ctx_t s_app;
static const char *TAG = "app_state";
#define STORE_MAGIC 0x554E4952u
#define STORE_VERSION 1
_Static_assert(sizeof(remote_t) == 104 && offsetof(remote_t, keys) == 20,
               "Version-1 remote profiles must retain their on-flash layout");

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t count;
    remote_t remotes[MAX_REMOTES];
    uint32_t checksum;
} remote_blob_t;

// Exact old layout. Keep its namespace after successful migration.
typedef struct { char name[8]; ir_nec_code_t code; } legacy_slot_t;
typedef struct { uint8_t count; legacy_slot_t slots[32]; } legacy_blob_t;

static const char *const key_names[] = {
    "", "POWER", "MODE", "CUST BTN 2", "CUST BTN 3", "UP", "LEFT", "OK",
    "RIGHT", "DOWN", "BACK", "PLAY/PAUSE", "HOME", "VOL UP", "CH UP",
    "VOL CLICK", "CH CLICK", "VOL DOWN", "CH DOWN", "CUST BTN 4",
    "CUST BTN 5", "CUST BTN 6",
};

app_ctx_t *app_get_ctx(void) { return &s_app; }
const char *remote_key_name(unsigned key)
{ return key <= REMOTE_KEY_COUNT ? key_names[key] : ""; }
const char *remote_transport_name(unsigned transport)
{
    static const char *const names[] = {"IR", "BT", "WIFI"};
    return transport <= REMOTE_WIFI ? names[transport] : "?";
}
bool remote_key_assignable(unsigned key)
{ return key >= 1 && key <= REMOTE_KEY_COUNT && key != REMOTE_KEY_MODE; }
bool remote_bt_has_identity(const remote_t *remote)
{
    const uint8_t *addr = remote->bt.local_addr;
    if ((addr[0] & 0xC0) != 0xC0) return false;
    bool all_zero = (addr[0] & 0x3F) == 0, all_one = (addr[0] & 0x3F) == 0x3F;
    for (unsigned i = 1; i < 6; ++i) {
        all_zero &= addr[i] == 0;
        all_one &= addr[i] == 0xFF;
    }
    return !all_zero && !all_one;
}
void remote_bt_reset_identity(remote_t *remote)
{
    uint8_t previous[6];
    memcpy(previous, remote->bt.local_addr, 6);
    do {
        esp_fill_random(remote->bt.local_addr, 6);
        remote->bt.local_addr[0] |= 0xC0;
    } while (!remote_bt_has_identity(remote) || memcmp(previous, remote->bt.local_addr, 6) == 0);
}
unsigned remote_assigned_count(const remote_t *remote)
{
    unsigned count = 0;
    uint32_t assigned = remote->transport == REMOTE_BT && remote->bt.kind == BT_REMOTE_KEYBOARD
        ? bt_keymap_mask() : remote->assigned;
    for (unsigned key = 1; key <= REMOTE_KEY_COUNT; ++key)
        if (remote_key_assignable(key) && (assigned & (1u << (key - 1)))) ++count;
    return count;
}
void app_post_evt(const app_evt_t *event)
{
    if (s_app.evt_q && xQueueSend(s_app.evt_q, event, pdMS_TO_TICKS(50)) != pdTRUE)
        ESP_LOGW(TAG, "Application event queue full");
}
bool app_send_cmd(const app_cmd_t *command, TickType_t timeout)
{ return s_app.cmd_q && xQueueSend(s_app.cmd_q, command, timeout) == pdTRUE; }
unsigned app_remote_count(void)
{
    xSemaphoreTake(s_app.model_mutex, portMAX_DELAY);
    unsigned count = s_app.remote_count;
    xSemaphoreGive(s_app.model_mutex);
    return count;
}
bool app_remote_get(unsigned index, remote_t *remote)
{
    xSemaphoreTake(s_app.model_mutex, portMAX_DELAY);
    bool valid = index < s_app.remote_count;
    if (valid) *remote = s_app.remotes[index];
    xSemaphoreGive(s_app.model_mutex);
    return valid;
}
static uint32_t checksum(const remote_blob_t *blob)
{
    const uint8_t *bytes = (const uint8_t *)blob;
    uint32_t hash = 2166136261u;
    for (size_t i = 0; i < offsetof(remote_blob_t, checksum); ++i)
        hash = (hash ^ bytes[i]) * 16777619u;
    return hash;
}
static bool valid_remote(const remote_t *remote)
{
    const uint32_t allowed = ((1u << REMOTE_KEY_COUNT) - 1u) & ~(1u << (REMOTE_KEY_MODE - 1));
    return remote->name[0] && memchr(remote->name, 0, sizeof(remote->name))
        && remote->transport <= REMOTE_WIFI && remote->icon < REMOTE_ICON_COUNT
        && !(remote->assigned & ~allowed)
        && (remote->transport != REMOTE_BT || (remote->bt.kind <= BT_REMOTE_CONTROLLER
            && remote->bt.bonded <= 1 && remote->bt.addr_type <= 3));
}
static esp_err_t save_remotes(void)
{
    remote_blob_t *blob = calloc(1, sizeof(*blob));
    if (!blob) return ESP_ERR_NO_MEM;
    blob->magic = STORE_MAGIC;
    blob->version = STORE_VERSION;
    xSemaphoreTake(s_app.model_mutex, portMAX_DELAY);
    blob->count = s_app.remote_count;
    memcpy(blob->remotes, s_app.remotes, sizeof(blob->remotes));
    xSemaphoreGive(s_app.model_mutex);
    blob->checksum = checksum(blob);
    nvs_handle_t handle;
    esp_err_t err = nvs_open("remotes", NVS_READWRITE, &handle);
    if (err == ESP_OK) {
        err = nvs_set_blob(handle, "profiles", blob, sizeof(*blob));
        if (err == ESP_OK) err = nvs_commit(handle);
        nvs_close(handle);
    }
    free(blob);
    if (err != ESP_OK) ESP_LOGE(TAG, "Saving remotes failed: %s", esp_err_to_name(err));
    return err;
}

// UI task owns mutations; restore RAM if durable saving fails.
esp_err_t app_remote_create(const remote_t *remote, unsigned *index)
{
    if (!valid_remote(remote)) return ESP_ERR_INVALID_ARG;
    if (s_app.storage_error != ESP_OK) return s_app.storage_error;
    unsigned count = app_remote_count();
    if (count >= MAX_REMOTES) return ESP_ERR_NO_MEM;
    xSemaphoreTake(s_app.model_mutex, portMAX_DELAY);
    s_app.remotes[count] = *remote;
    s_app.remote_count++;
    xSemaphoreGive(s_app.model_mutex);
    esp_err_t err = save_remotes();
    if (err != ESP_OK) {
        xSemaphoreTake(s_app.model_mutex, portMAX_DELAY);
        s_app.remote_count--;
        memset(&s_app.remotes[count], 0, sizeof(remote_t));
        xSemaphoreGive(s_app.model_mutex);
    } else if (index) *index = count;
    return err;
}
esp_err_t app_remote_update(unsigned index, const remote_t *remote)
{
    if (!valid_remote(remote)) return ESP_ERR_INVALID_ARG;
    if (s_app.storage_error != ESP_OK) return s_app.storage_error;
    remote_t previous;
    if (!app_remote_get(index, &previous)) return ESP_ERR_INVALID_ARG;
    xSemaphoreTake(s_app.model_mutex, portMAX_DELAY);
    s_app.remotes[index] = *remote;
    xSemaphoreGive(s_app.model_mutex);
    esp_err_t err = save_remotes();
    if (err != ESP_OK) {
        xSemaphoreTake(s_app.model_mutex, portMAX_DELAY);
        s_app.remotes[index] = previous;
        xSemaphoreGive(s_app.model_mutex);
    }
    return err;
}
esp_err_t app_remote_remove(unsigned index)
{
    if (s_app.storage_error != ESP_OK) return s_app.storage_error;
    remote_t previous;
    if (!app_remote_get(index, &previous)) return ESP_ERR_INVALID_ARG;
    xSemaphoreTake(s_app.model_mutex, portMAX_DELAY);
    unsigned count = --s_app.remote_count;
    memmove(&s_app.remotes[index], &s_app.remotes[index + 1], (count - index) * sizeof(remote_t));
    memset(&s_app.remotes[count], 0, sizeof(remote_t));
    xSemaphoreGive(s_app.model_mutex);
    esp_err_t err = save_remotes();
    if (err != ESP_OK) {
        xSemaphoreTake(s_app.model_mutex, portMAX_DELAY);
        memmove(&s_app.remotes[index + 1], &s_app.remotes[index], (count - index) * sizeof(remote_t));
        s_app.remotes[index] = previous;
        s_app.remote_count++;
        xSemaphoreGive(s_app.model_mutex);
    }
    return err;
}
esp_err_t app_remote_set_code(unsigned index, unsigned key, ir_nec_code_t code)
{
    remote_t remote;
    if (!remote_key_assignable(key) || !app_remote_get(index, &remote) || remote.transport != REMOTE_IR)
        return ESP_ERR_INVALID_ARG;
    remote.keys[key - 1] = code;
    remote.assigned |= 1u << (key - 1);
    return app_remote_update(index, &remote);
}
static esp_err_t migrate_legacy(void)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open("ir", NVS_READONLY, &handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) return ESP_OK;
    if (err != ESP_OK) return err;
    legacy_blob_t old = {0};
    size_t size = sizeof(old);
    err = nvs_get_blob(handle, "slots", &old, &size);
    nvs_close(handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) return ESP_OK;
    if (err != ESP_OK) return err;
    if (size != sizeof(old) || old.count > 32) return ESP_ERR_INVALID_SIZE;
    unsigned slot = 0;
    while (slot < old.count) {
        remote_t *remote = &s_app.remotes[s_app.remote_count++];
        snprintf(remote->name, sizeof(remote->name), "Saved IR %u", s_app.remote_count);
        remote->transport = REMOTE_IR;
        for (unsigned key = 1; key <= REMOTE_KEY_COUNT && slot < old.count; ++key) {
            if (!remote_key_assignable(key)) continue;
            remote->keys[key - 1] = old.slots[slot++].code;
            remote->assigned |= 1u << (key - 1);
        }
    }
    return old.count ? save_remotes() : ESP_OK;
}
static esp_err_t load_remotes(void)
{
    // Do not erase recordings automatically when NVS needs recovery.
    esp_err_t err = nvs_flash_init();
    if (err != ESP_OK) return err;
    nvs_handle_t handle;
    err = nvs_open("remotes", NVS_READONLY, &handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) return migrate_legacy();
    if (err != ESP_OK) return err;
    remote_blob_t *blob = calloc(1, sizeof(*blob));
    if (!blob) { nvs_close(handle); return ESP_ERR_NO_MEM; }
    size_t size = sizeof(*blob);
    err = nvs_get_blob(handle, "profiles", blob, &size);
    nvs_close(handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) { free(blob); return migrate_legacy(); }
    if (err == ESP_OK && (size != sizeof(*blob) || blob->magic != STORE_MAGIC
        || blob->version != STORE_VERSION || blob->count > MAX_REMOTES || blob->checksum != checksum(blob)))
        err = ESP_ERR_INVALID_STATE;
    if (err == ESP_OK) {
        for (unsigned i = 0; i < blob->count; ++i)
            if (!valid_remote(&blob->remotes[i])) { err = ESP_ERR_INVALID_ARG; break; }
    }
    if (err == ESP_OK) {
        s_app.remote_count = blob->count;
        memcpy(s_app.remotes, blob->remotes, sizeof(s_app.remotes));
    }
    free(blob);
    return err;
}
void app_state_init(void)
{
    memset(&s_app, 0, sizeof(s_app));
    s_app.cmd_q = xQueueCreate(16, sizeof(app_cmd_t));
    s_app.evt_q = xQueueCreate(16, sizeof(app_evt_t));
    s_app.model_mutex = xSemaphoreCreateMutex();
    ESP_ERROR_CHECK(s_app.cmd_q && s_app.evt_q && s_app.model_mutex ? ESP_OK : ESP_ERR_NO_MEM);
    s_app.storage_error = load_remotes();
    if (s_app.storage_error != ESP_OK)
        ESP_LOGE(TAG, "Remote storage unavailable: %s", esp_err_to_name(s_app.storage_error));
}
