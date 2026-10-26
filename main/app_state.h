#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "esp_err.h"

#define REMOTE_KEY_COUNT 21
#define REMOTE_NAME_MAX 12
#define REMOTE_ICON_COUNT 24
#define MAX_REMOTES 16

// Values are the physical key numbers in the PCB photograph.
typedef enum {
    REMOTE_KEY_POWER = 1, REMOTE_KEY_MODE, REMOTE_KEY_CUSTOM2,
    REMOTE_KEY_CUSTOM3, REMOTE_KEY_UP, REMOTE_KEY_LEFT, REMOTE_KEY_OK,
    REMOTE_KEY_RIGHT, REMOTE_KEY_DOWN, REMOTE_KEY_BACK,
    REMOTE_KEY_PLAY_PAUSE, REMOTE_KEY_HOME, REMOTE_KEY_VOL_UP,
    REMOTE_KEY_CH_UP, REMOTE_KEY_VOL_CLICK, REMOTE_KEY_CH_CLICK,
    REMOTE_KEY_VOL_DOWN, REMOTE_KEY_CH_DOWN, REMOTE_KEY_CUSTOM4,
    REMOTE_KEY_CUSTOM5, REMOTE_KEY_CUSTOM6,
} remote_key_t;

typedef enum { REMOTE_IR = 0, REMOTE_BT, REMOTE_WIFI } remote_transport_t;
typedef struct { uint16_t addr; uint16_t cmd; } ir_nec_code_t;
typedef struct {
    char name[REMOTE_NAME_MAX + 1];
    uint8_t transport;
    uint8_t icon;
    uint32_t assigned;
    ir_nec_code_t keys[REMOTE_KEY_COUNT];
} remote_t;

typedef enum {
    APP_CMD_IR_LEARN_ONESHOT = 1, APP_CMD_IR_TX_SEND, APP_CMD_IR_LEARN_CANCEL,
} app_cmd_type_t;
typedef struct {
    app_cmd_type_t type;
    uint32_t request_id;
    union {
        struct { uint32_t timeout_ms; } learn;
        struct { ir_nec_code_t code; } tx;
    };
} app_cmd_t;
typedef enum {
    APP_EVT_IR_LEARNED = 1, APP_EVT_IR_LEARN_TIMEOUT, APP_EVT_IR_TX_DONE,
    APP_EVT_IR_TX_FAIL, APP_EVT_IR_LEARN_FAILED, APP_EVT_IR_UNSUPPORTED,
} app_evt_type_t;
typedef struct {
    app_evt_type_t type;
    uint32_t request_id;
    union { struct { ir_nec_code_t code; } ir; };
} app_evt_t;
typedef struct {
    QueueHandle_t cmd_q;
    QueueHandle_t evt_q;
    SemaphoreHandle_t model_mutex;
    remote_t remotes[MAX_REMOTES];
    uint8_t remote_count;
    esp_err_t storage_error;
} app_ctx_t;

app_ctx_t *app_get_ctx(void);
void app_state_init(void);
void app_post_evt(const app_evt_t *event);
bool app_send_cmd(const app_cmd_t *command, TickType_t timeout);
const char *remote_key_name(unsigned key);
const char *remote_transport_name(unsigned transport);
bool remote_key_assignable(unsigned key);
unsigned remote_assigned_count(const remote_t *remote);
bool app_remote_get(unsigned index, remote_t *remote);
unsigned app_remote_count(void);
esp_err_t app_remote_create(const remote_t *remote, unsigned *index);
esp_err_t app_remote_update(unsigned index, const remote_t *remote);
esp_err_t app_remote_remove(unsigned index);
esp_err_t app_remote_set_code(unsigned index, unsigned key, ir_nec_code_t code);
