#pragma once
#include <stdbool.h>
#include "ui_events.h"
typedef struct u8g2_struct u8g2_t;
typedef enum {
    UI_SCREEN_SPLASH = 0, UI_SCREEN_HOME, UI_SCREEN_REMOTE, UI_SCREEN_EDIT,
    UI_SCREEN_ADD_TYPE, UI_SCREEN_NAME, UI_SCREEN_ICON, UI_SCREEN_IR_KEYS,
    UI_SCREEN_IR_REPLACE, UI_SCREEN_IR_WAIT, UI_SCREEN_IR_RESULT,
    UI_SCREEN_REMOVE, UI_SCREEN_CONSTRUCTION, UI_SCREEN_BT_TYPE,
    UI_SCREEN_BT_DEVICE, UI_SCREEN_BT_CONFIRM, UI_SCREEN_BT_KEYS,
    UI_SCREEN_BT_PRESET, UI_SCREEN_BT_BINDING, UI_SCREEN_BT_TEXT, UI_SCREEN_COUNT,
} ui_screen_id_t;
struct ui_ctx;
typedef struct {
    ui_screen_id_t id;
    void (*on_enter)(struct ui_ctx *ctx, void *param);
    void (*on_exit)(struct ui_ctx *ctx);
    void (*handle)(struct ui_ctx *ctx, const ui_event_t *event);
    void (*render)(struct ui_ctx *ctx, u8g2_t *display);
} ui_screen_t;
