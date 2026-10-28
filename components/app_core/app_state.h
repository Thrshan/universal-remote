#pragma once
#include "remote_model.h"
#include "app_events.h"
#include "app_storage.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"

typedef struct {
    QueueHandle_t cmd_q;
    QueueHandle_t evt_q;
    SemaphoreHandle_t model_mutex;
    remote_t remotes[MAX_REMOTES];
    char bt_text[MAX_REMOTES][REMOTE_KEY_COUNT][BT_TEXT_MAX + 1];
    uint8_t remote_count;
    esp_err_t storage_error;
} app_ctx_t;

app_ctx_t *app_get_ctx(void);
void app_state_init(void);
void app_post_evt(const app_evt_t *event);
bool app_send_cmd(const app_cmd_t *command, TickType_t timeout);
bool app_remote_get(unsigned index, remote_t *remote);
unsigned app_remote_count(void);
esp_err_t app_remote_create(const remote_t *remote, unsigned *index);
esp_err_t app_remote_update(unsigned index, const remote_t *remote);
esp_err_t app_remote_remove(unsigned index);
esp_err_t app_remote_set_code(unsigned index, unsigned key, ir_nec_code_t code);
esp_err_t app_remote_set_bt_binding(unsigned index, unsigned key, bt_binding_t binding);
esp_err_t app_remote_set_bt_text(unsigned index, unsigned key, const char *text);
bool app_remote_get_bt_text(unsigned index, unsigned key, char text[BT_TEXT_MAX + 1]);
