#pragma once
#include "remote_model.h"

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
    APP_EVT_BT_PEER, APP_EVT_BT_TX_DONE, APP_EVT_BT_TX_FAIL,
} app_evt_type_t;
typedef struct {
    app_evt_type_t type;
    uint32_t request_id;
    union {
        struct { ir_nec_code_t code; } ir;
        struct { bool bonded; uint8_t addr_type; uint8_t peer[6]; } bt;
    };
} app_evt_t;
