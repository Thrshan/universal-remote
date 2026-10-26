#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"
#include "esp_err.h"

typedef struct {
    uint16_t addr;
    uint16_t  cmd;
    //TODO uint8_t  cmd or uint16_t  cmd
} ir_nec_code_t;

typedef enum {
    APP_CMD_IR_LEARN_ONESHOT = 1,
    APP_CMD_IR_TX_SEND,
    APP_CMD_IR_LEARN_CANCEL,
} app_cmd_type_t;

typedef struct {
    app_cmd_type_t type;
    union {
        struct { uint32_t timeout_ms; } learn;
        struct { ir_nec_code_t code; } tx;
    };
} app_cmd_t;

typedef enum {
    APP_EVT_IR_LEARNED = 1,
    APP_EVT_IR_LEARN_TIMEOUT,
    APP_EVT_IR_TX_DONE,
    APP_EVT_IR_TX_FAIL,
} app_evt_type_t;

typedef struct {
    app_evt_type_t type;
    union {
        struct { ir_nec_code_t code; } ir;
    };
} app_evt_t;

#define MAX_IR_SLOTS 32

typedef struct {
    char name[8];           // "IR_1" etc
    ir_nec_code_t code;
} ir_slot_t;

typedef struct {
    // queues
    QueueHandle_t cmd_q;
    QueueHandle_t evt_q;

    // model
    SemaphoreHandle_t model_mutex;
    ir_slot_t ir_slots[MAX_IR_SLOTS];
    uint8_t ir_count;       // number of recorded IRs

    // last event snapshot for UI convenience
    app_evt_t last_evt;

} app_ctx_t;

app_ctx_t *app_get_ctx(void);
void app_post_evt(const app_evt_t *e);
bool app_send_cmd(const app_cmd_t *c, TickType_t to);
void app_state_init(void);  // ADD THIS LINE
esp_err_t app_state_load_ir(void);
esp_err_t app_state_save_ir(void);