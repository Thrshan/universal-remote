// ===================== components/ui/include/ui_events.h =====================
#pragma once
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    UI_EV_NONE = 0,

    // Navigation
    UI_EV_UP,
    UI_EV_DOWN,
    UI_EV_LEFT,
    UI_EV_RIGHT,
    UI_EV_OK,
    UI_EV_BACK,

    // Press variants
    UI_EV_OK_LONG,
    UI_EV_BACK_LONG,

    // System
    UI_EV_TICK,        // periodic (cursor blink / animation)
    UI_EV_DIRTY,       // force redraw
} ui_event_type_t;

typedef enum {
    UI_SRC_KEYPAD = 0,
    UI_SRC_IR,
    UI_SRC_BLE,
    UI_SRC_ENCODER,
    UI_SRC_SYSTEM,
} ui_event_src_t;

typedef struct {
    ui_event_type_t type;
    ui_event_src_t  src;
    uint32_t        ts_ms;     // timestamp (xTaskGetTickCount()*portTICK_PERIOD_MS)
    int32_t         value;     // optional: encoder delta, etc.
} ui_event_t;

#ifdef __cplusplus
}
#endif
