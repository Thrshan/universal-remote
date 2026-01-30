#pragma once
#include <stdint.h>

typedef enum {
    APP_EVT_NONE = 0,
    APP_EVT_KEY,        // a key press from your keypad
    APP_EVT_MODE,       // change mode
    APP_EVT_IR_LEARNED, // received IR code in LEARN mode
    APP_EVT_SEND_IR,    // request to send IR
    APP_EVT_BT_VOL_TICK // request to send volume report
} app_evt_type_t;

typedef enum {
    APP_MODE_NORMAL = 0,
    APP_MODE_LEARN  = 1,
    APP_MODE_SEND   = 2
} app_mode_t;

typedef struct {
    uint32_t addr;
    uint32_t cmd;
} ir_nec_code_t;

typedef struct {
    app_evt_type_t type;
    union {
        struct { int key_id; } key;
        struct { app_mode_t mode; } mode;
        struct { ir_nec_code_t code; } ir;
        struct { ir_nec_code_t code; } send_ir;
    };
} app_evt_t;
