#include "app_state.h"
#include "app_storage_internal.h"
#include "bt_keymap.h"
#include "esp_log.h"
#include <string.h>
#include <stdio.h>

static app_ctx_t s_app;
static const char *TAG = "app_state";

// The UI task owns mutations and restores RAM when a durable save fails.
app_ctx_t *app_get_ctx(void) { return &s_app; }

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

esp_err_t app_remote_create(const remote_t *remote, unsigned *index)
{
    if (!remote_valid(remote)) return ESP_ERR_INVALID_ARG;
    if (s_app.storage_error != ESP_OK) return s_app.storage_error;
    unsigned count = app_remote_count();
    if (count >= MAX_REMOTES) return ESP_ERR_NO_MEM;
    xSemaphoreTake(s_app.model_mutex, portMAX_DELAY);
    s_app.remotes[count] = *remote;
    memset(s_app.bt_text[count], 0, sizeof(s_app.bt_text[count]));
    s_app.remote_count++;
    xSemaphoreGive(s_app.model_mutex);
    esp_err_t err = app_storage_save(&s_app);
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
    if (!remote_valid(remote)) return ESP_ERR_INVALID_ARG;
    if (s_app.storage_error != ESP_OK) return s_app.storage_error;
    remote_t previous;
    if (!app_remote_get(index, &previous)) return ESP_ERR_INVALID_ARG;
    xSemaphoreTake(s_app.model_mutex, portMAX_DELAY);
    s_app.remotes[index] = *remote;
    xSemaphoreGive(s_app.model_mutex);
    esp_err_t err = app_storage_save(&s_app);
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
    char previous_text[REMOTE_KEY_COUNT][BT_TEXT_MAX + 1];
    memcpy(previous_text, s_app.bt_text[index], sizeof(previous_text));
    xSemaphoreTake(s_app.model_mutex, portMAX_DELAY);
    unsigned count = --s_app.remote_count;
    memmove(&s_app.remotes[index], &s_app.remotes[index + 1], (count - index) * sizeof(remote_t));
    memset(&s_app.remotes[count], 0, sizeof(remote_t));
    memmove(&s_app.bt_text[index], &s_app.bt_text[index + 1], (count - index) * sizeof(s_app.bt_text[0]));
    memset(&s_app.bt_text[count], 0, sizeof(s_app.bt_text[0]));
    xSemaphoreGive(s_app.model_mutex);
    esp_err_t err = app_storage_save(&s_app);
    if (err != ESP_OK) {
        xSemaphoreTake(s_app.model_mutex, portMAX_DELAY);
        memmove(&s_app.remotes[index + 1], &s_app.remotes[index], (count - index) * sizeof(remote_t));
        s_app.remotes[index] = previous;
        memmove(&s_app.bt_text[index + 1], &s_app.bt_text[index], (count - index) * sizeof(s_app.bt_text[0]));
        memcpy(s_app.bt_text[index], previous_text, sizeof(previous_text));
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

esp_err_t app_remote_set_bt_binding(unsigned index, unsigned key, bt_binding_t binding)
{
    remote_t remote;
    if (!remote_key_assignable(key) || !bt_binding_valid(binding) || !app_remote_get(index, &remote)
        || remote.transport != REMOTE_BT || remote.bt.kind != BT_REMOTE_KEYBOARD || remote.bt.preset != BT_PRESET_CUSTOM) return ESP_ERR_INVALID_ARG;
    remote.bt.bindings[key - 1] = binding;
    remote.assigned = bt_profile_mask(&remote);
    return app_remote_update(index, &remote);
}

bool app_remote_get_bt_text(unsigned index, unsigned key, char text[BT_TEXT_MAX + 1])
{
    if (!remote_key_assignable(key)) return false;
    xSemaphoreTake(s_app.model_mutex, portMAX_DELAY);
    bool valid = index < s_app.remote_count && s_app.remotes[index].transport == REMOTE_BT
        && s_app.remotes[index].bt.kind == BT_REMOTE_KEYBOARD && s_app.remotes[index].bt.preset == BT_PRESET_TEXT;
    if (valid) memcpy(text, s_app.bt_text[index][key - 1], BT_TEXT_MAX + 1);
    xSemaphoreGive(s_app.model_mutex);
    return valid;
}

esp_err_t app_remote_set_bt_text(unsigned index, unsigned key, const char *text)
{
    remote_t remote;
    if (!remote_key_assignable(key) || !bt_text_valid(text) || !app_remote_get(index, &remote)
        || remote.transport != REMOTE_BT || remote.bt.kind != BT_REMOTE_KEYBOARD || remote.bt.preset != BT_PRESET_TEXT) return ESP_ERR_INVALID_ARG;
    char previous[BT_TEXT_MAX + 1];
    xSemaphoreTake(s_app.model_mutex, portMAX_DELAY);
    memcpy(previous, s_app.bt_text[index][key - 1], sizeof(previous));
    snprintf(s_app.bt_text[index][key - 1], BT_TEXT_MAX + 1, "%s", text);
    xSemaphoreGive(s_app.model_mutex);
    if (text[0]) remote.assigned |= 1u << (key - 1);
    else remote.assigned &= ~(1u << (key - 1));
    esp_err_t err = app_remote_update(index, &remote);
    if (err != ESP_OK) {
        xSemaphoreTake(s_app.model_mutex, portMAX_DELAY);
        memcpy(s_app.bt_text[index][key - 1], previous, sizeof(previous));
        xSemaphoreGive(s_app.model_mutex);
    }
    return err;
}

void app_state_init(void)
{
    memset(&s_app, 0, sizeof(s_app));
    s_app.cmd_q = xQueueCreate(16, sizeof(app_cmd_t));
    s_app.evt_q = xQueueCreate(16, sizeof(app_evt_t));
    s_app.model_mutex = xSemaphoreCreateMutex();
    ESP_ERROR_CHECK(s_app.cmd_q && s_app.evt_q && s_app.model_mutex ? ESP_OK : ESP_ERR_NO_MEM);
    s_app.storage_error = app_storage_load(&s_app);
    if (s_app.storage_error != ESP_OK)
        ESP_LOGE(TAG, "Remote storage unavailable: %s", esp_err_to_name(s_app.storage_error));
}
