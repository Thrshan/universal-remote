// ===================== components/ui/include/ui_screen.h =====================
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "ui_events.h"

#ifdef __cplusplus
extern "C" {
#endif

// Forward-declare u8g2 without exposing its header everywhere
typedef struct u8g2_struct u8g2_t;

typedef enum {
    UI_SCREEN_HOME = 0,
    UI_SCREEN_MENU,
    UI_SCREEN_ITEMS,
    UI_SCREEN_ADD_TYPE,
    UI_SCREEN_ADD_EDIT,
    UI_SCREEN_DIALOG,
    UI_SCREEN_IR_LEARN,
    UI_SCREEN_IR_LIST,
    UI_SCREEN_COUNT,
} ui_screen_id_t;

struct ui_ctx; // forward

typedef struct {
    ui_screen_id_t id;

    void (*on_enter)(struct ui_ctx *ctx, void *param);
    void (*on_exit) (struct ui_ctx *ctx);
    void (*handle)  (struct ui_ctx *ctx, const ui_event_t *ev);
    void (*render)  (struct ui_ctx *ctx, u8g2_t *u8);

    // Per-screen private state pointer (points to a struct owned by ctx)
    void *state;
} ui_screen_t;

#ifdef __cplusplus
}
#endif
