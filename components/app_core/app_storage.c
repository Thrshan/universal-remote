#include "app_storage_internal.h"
#include "bt_keymap.h"
#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static const char *TAG = "app_state";
#define STORE_MAGIC 0x554E4952u
#define STORE_VERSION 3
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

static esp_err_t namespace_usage(const char *name, size_t *entries)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(name, NVS_READONLY, &handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) return ESP_OK;
    if (err != ESP_OK) return err;
    size_t used = 0;
    err = nvs_get_used_entry_count(handle, &used);
    nvs_close(handle);
    if (err == ESP_OK) *entries += used + 1; // Include the namespace entry.
    return err;
}

esp_err_t app_storage_get_usage(app_storage_usage_t *usage)
{
    if (!usage) return ESP_ERR_INVALID_ARG;
    memset(usage, 0, sizeof(*usage));
    nvs_stats_t stats;
    esp_err_t err = nvs_get_stats(NULL, &stats);
    if (err != ESP_OK) return err;
    size_t entries = 0;
    // Profiles, retained legacy recordings, HID subscriptions and BLE bonds
    // share NVS. Include the allocated entries, not just the blob's payload.
    static const char *const namespaces[] = {"remotes", "ir", "ur_hid", "bt_config.conf", "ur_public"};
    for (unsigned i = 0; i < sizeof(namespaces) / sizeof(namespaces[0]); ++i) {
        err = namespace_usage(namespaces[i], &entries);
        if (err != ESP_OK) return err;
    }
    for (unsigned i = 0; i < app_remote_count(); ++i) {
        remote_t remote;
        if (!app_remote_get(i, &remote) || remote.transport != REMOTE_BT
            || remote.bt.bond_store != BT_BOND_STORE_PROFILE || !remote_bt_has_identity(&remote)) continue;
        bool counted = false;
        for (unsigned j = 0; j < i; ++j) {
            remote_t previous;
            if (app_remote_get(j, &previous) && previous.transport == REMOTE_BT
                && previous.bt.bond_store == BT_BOND_STORE_PROFILE
                && memcmp(previous.bt.local_addr, remote.bt.local_addr, 6) == 0) counted = true;
        }
        if (counted) continue;
        const uint8_t *a = remote.bt.local_addr;
        char name[15];
        snprintf(name, sizeof(name), "ur%02x%02x%02x%02x%02x%02x", a[0], a[1], a[2], a[3], a[4], a[5]);
        err = namespace_usage(name, &entries);
        if (err != ESP_OK) return err;
    }
    // NVS entries occupy 32 bytes. Available excludes NVS's reserved page and
    // other system data, so free space is not total flash minus remote bytes.
    usage->remote_bytes = entries * 32;
    usage->free_bytes = stats.available_entries * 32;
    return ESP_OK;
}

static uint32_t checksum(const remote_blob_t *blob, size_t size)
{
    const uint8_t *bytes = (const uint8_t *)blob;
    uint32_t hash = 2166136261u;
    for (size_t i = 0; i < size; ++i)
        if (i < offsetof(remote_blob_t, checksum) || i >= sizeof(*blob))
            hash = (hash ^ bytes[i]) * 16777619u;
    return hash;
}

esp_err_t app_storage_save(app_ctx_t *app)
{
    // Append only nonempty strings to the checksummed profile snapshot. Names,
    // mappings, text and peer metadata are committed as one NVS value.
    size_t size = sizeof(remote_blob_t);
    for (unsigned i = 0; i < app->remote_count; ++i)
        if (app->remotes[i].transport == REMOTE_BT && app->remotes[i].bt.preset == BT_PRESET_TEXT)
            for (unsigned key = 0; key < REMOTE_KEY_COUNT; ++key)
                if (app->bt_text[i][key][0]) size += 3 + strlen(app->bt_text[i][key]);
    remote_blob_t *blob = calloc(1, size);
    if (!blob) return ESP_ERR_NO_MEM;
    blob->magic = STORE_MAGIC;
    blob->version = STORE_VERSION;
    xSemaphoreTake(app->model_mutex, portMAX_DELAY);
    blob->count = app->remote_count;
    memcpy(blob->remotes, app->remotes, sizeof(blob->remotes));
    uint8_t *text = (uint8_t *)(blob + 1);
    for (unsigned i = 0; i < app->remote_count; ++i)
        if (app->remotes[i].transport == REMOTE_BT && app->remotes[i].bt.preset == BT_PRESET_TEXT)
            for (unsigned key = 0; key < REMOTE_KEY_COUNT; ++key) {
                size_t length = strlen(app->bt_text[i][key]);
                if (!length) continue;
                *text++ = i; *text++ = key; *text++ = length;
                memcpy(text, app->bt_text[i][key], length);
                text += length;
            }
    xSemaphoreGive(app->model_mutex);
    blob->checksum = checksum(blob, size);
    nvs_handle_t handle;
    esp_err_t err = nvs_open("remotes", NVS_READWRITE, &handle);
    if (err == ESP_OK) {
        err = nvs_set_blob(handle, "profiles", blob, size);
        if (err == ESP_OK) err = nvs_commit(handle);
        nvs_close(handle);
    }
    free(blob);
    if (err != ESP_OK) ESP_LOGE(TAG, "Saving remotes failed: %s", esp_err_to_name(err));
    return err;
}

static esp_err_t migrate_legacy(app_ctx_t *app)
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
        remote_t *remote = &app->remotes[app->remote_count++];
        snprintf(remote->name, sizeof(remote->name), "Saved IR %u", app->remote_count);
        remote->transport = REMOTE_IR;
        for (unsigned key = 1; key <= REMOTE_KEY_COUNT && slot < old.count; ++key) {
            if (!remote_key_assignable(key)) continue;
            remote->keys[key - 1] = old.slots[slot++].code;
            remote->assigned |= 1u << (key - 1);
        }
    }
    return old.count ? app_storage_save(app) : ESP_OK;
}

esp_err_t app_storage_load(app_ctx_t *app)
{
    // Do not erase recordings automatically when NVS needs recovery.
    esp_err_t err = nvs_flash_init();
    if (err != ESP_OK) return err;
    nvs_handle_t handle;
    err = nvs_open("remotes", NVS_READONLY, &handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) return migrate_legacy(app);
    if (err != ESP_OK) return err;
    size_t size = 0;
    err = nvs_get_blob(handle, "profiles", NULL, &size);
    if (err == ESP_ERR_NVS_NOT_FOUND) { nvs_close(handle); return migrate_legacy(app); }
    const size_t max_size = sizeof(remote_blob_t) + MAX_REMOTES * REMOTE_KEY_COUNT * (3 + BT_TEXT_MAX);
    if (err != ESP_OK || size < sizeof(remote_blob_t) || size > max_size) {
        nvs_close(handle); return err == ESP_OK ? ESP_ERR_INVALID_SIZE : err;
    }
    remote_blob_t *blob = calloc(1, size);
    if (!blob) { nvs_close(handle); return ESP_ERR_NO_MEM; }
    err = nvs_get_blob(handle, "profiles", blob, &size);
    nvs_close(handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) { free(blob); return migrate_legacy(app); }
    if (err == ESP_OK && (blob->magic != STORE_MAGIC
        || (blob->version != 1 && blob->version != 2 && blob->version != STORE_VERSION)
        || (blob->version == 1 && size != sizeof(*blob))
        || blob->count > MAX_REMOTES || blob->checksum != checksum(blob, size)))
        err = ESP_ERR_INVALID_STATE;
    if (err == ESP_OK) {
        const uint8_t *text = (const uint8_t *)(blob + 1), *end = (const uint8_t *)blob + size;
        while (text < end && err == ESP_OK) {
            if ((size_t)(end - text) < 3) { err = ESP_ERR_INVALID_SIZE; break; }
            unsigned index = *text++, key = *text++, length = *text++;
            if (index >= blob->count || key >= REMOTE_KEY_COUNT || !remote_key_assignable(key + 1)
                || !length || length > BT_TEXT_MAX || (size_t)(end - text) < length
                || blob->remotes[index].transport != REMOTE_BT || blob->remotes[index].bt.preset != BT_PRESET_TEXT
                || app->bt_text[index][key][0] || memchr(text, 0, length)) { err = ESP_ERR_INVALID_ARG; break; }
            memcpy(app->bt_text[index][key], text, length);
            if (!bt_text_valid(app->bt_text[index][key])) err = ESP_ERR_INVALID_ARG;
            text += length;
        }
        for (unsigned i = 0; i < blob->count && err == ESP_OK; ++i)
            if (blob->remotes[i].transport == REMOTE_BT && blob->remotes[i].bt.preset == BT_PRESET_TEXT)
                for (unsigned key = 1; key <= REMOTE_KEY_COUNT; ++key)
                    if (!!(blob->remotes[i].assigned & (1u << (key - 1))) != !!app->bt_text[i][key - 1][0])
                        err = ESP_ERR_INVALID_ARG;
    }
    if (err == ESP_OK && blob->version < 3)
        for (unsigned i = 0; i < blob->count; ++i)
            if (blob->remotes[i].transport == REMOTE_BT)
                blob->remotes[i].bt.bond_store = BT_BOND_STORE_LEGACY;
    if (err == ESP_OK) {
        for (unsigned i = 0; i < blob->count; ++i)
            if (!remote_valid(&blob->remotes[i])) { err = ESP_ERR_INVALID_ARG; break; }
    }
    if (err == ESP_OK) {
        app->remote_count = blob->count;
        memcpy(app->remotes, blob->remotes, sizeof(app->remotes));
    }
    free(blob);
    return err;
}
