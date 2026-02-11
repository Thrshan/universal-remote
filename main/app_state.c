#include "app_state.h"
#include <string.h>
#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"

static app_ctx_t s_app;
static const char *TAG = "app_state";

#define IR_NVS_NAMESPACE "ir"
#define IR_NVS_KEY       "slots"

typedef struct {
    uint8_t count;
    ir_slot_t slots[MAX_IR_SLOTS];
} ir_nvs_blob_t;

app_ctx_t *app_get_ctx(void) { return &s_app; }

void app_post_evt(const app_evt_t *e) {
    if (!s_app.evt_q) return;
    (void)xQueueSend(s_app.evt_q, e, 0);
}

bool app_send_cmd(const app_cmd_t *c, TickType_t to) {
    if (!s_app.cmd_q) return false;
    return xQueueSend(s_app.cmd_q, c, to) == pdTRUE;
}

bool app_read_evt(app_evt_t *e, TickType_t to) {
    return (xQueueReceive(s_app.evt_q, e, to) == pdTRUE);
}

static esp_err_t app_nvs_init_once(void) {
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS init needs erase: %s", esp_err_to_name(err));
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NVS init failed: %s", esp_err_to_name(err));
    }
    return err;
}

esp_err_t app_state_load_ir(void) {
    esp_err_t err = app_nvs_init_once();
    if (err != ESP_OK) return err;

    nvs_handle_t nvs = 0;
    err = nvs_open(IR_NVS_NAMESPACE, NVS_READONLY, &nvs);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        return ESP_OK; // no data yet
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_open failed: %s", esp_err_to_name(err));
        return err;
    }

    ir_nvs_blob_t blob = {0};
    size_t size = sizeof(blob);
    err = nvs_get_blob(nvs, IR_NVS_KEY, &blob, &size);
    nvs_close(nvs);

    if (err == ESP_ERR_NVS_NOT_FOUND) {
        return ESP_OK;
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_get_blob failed: %s", esp_err_to_name(err));
        return err;
    }
    if (size != sizeof(blob)) {
        ESP_LOGW(TAG, "NVS blob size mismatch (%u)", (unsigned)size);
        return ESP_ERR_INVALID_SIZE;
    }

    xSemaphoreTake(s_app.model_mutex, portMAX_DELAY);
    s_app.ir_count = (blob.count > MAX_IR_SLOTS) ? MAX_IR_SLOTS : blob.count;
    memcpy(s_app.ir_slots, blob.slots, sizeof(s_app.ir_slots));
    xSemaphoreGive(s_app.model_mutex);

    ESP_LOGI(TAG, "Loaded %u IR slots from NVS", (unsigned)s_app.ir_count);
    return ESP_OK;
}

esp_err_t app_state_save_ir(void) {
    esp_err_t err = app_nvs_init_once();
    if (err != ESP_OK) return err;

    ir_nvs_blob_t blob = {0};
    xSemaphoreTake(s_app.model_mutex, portMAX_DELAY);
    blob.count = s_app.ir_count;
    memcpy(blob.slots, s_app.ir_slots, sizeof(s_app.ir_slots));
    xSemaphoreGive(s_app.model_mutex);

    nvs_handle_t nvs = 0;
    err = nvs_open(IR_NVS_NAMESPACE, NVS_READWRITE, &nvs);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_open failed: %s", esp_err_to_name(err));
        return err;
    }

    err = nvs_set_blob(nvs, IR_NVS_KEY, &blob, sizeof(blob));
    if (err == ESP_OK) {
        err = nvs_commit(nvs);
    }
    nvs_close(nvs);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_set_blob failed: %s", esp_err_to_name(err));
    }
    return err;
}

// call this once in app_main()
void app_state_init(void) {
    memset(&s_app, 0, sizeof(s_app));
    s_app.cmd_q = xQueueCreate(16, sizeof(app_cmd_t));
    s_app.evt_q = xQueueCreate(16, sizeof(app_evt_t));
    s_app.model_mutex = xSemaphoreCreateMutex();
    (void)app_state_load_ir();
}
