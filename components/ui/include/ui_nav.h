// ===================== components/ui/include/ui_nav.h =====================
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "ui_screen.h"
#include "app_state.h" // NEW (for ir_nec_code_t)

#ifdef __cplusplus
extern "C" {
#endif

#define UI_STACK_MAX 8

typedef struct {
    ui_screen_id_t id;
    void *param; // optional: pointer passed on_enter (can be NULL)
} ui_stack_entry_t;

    typedef struct {
    bool show;
    uint32_t until_ms;
    ir_nec_code_t last_code;
} ui_ir_banner_t;

typedef struct ui_ctx {
    // u8g2 pointer owned by the display task
    void *u8g2; // stored as void* to avoid pulling u8g2.h into every file

    // registered screens
    ui_screen_t screens[UI_SCREEN_COUNT];

    // navigation stack
    ui_stack_entry_t stack[UI_STACK_MAX];
    int top; // -1 when empty

    // dirty flag: render only when dirty or on tick animation
    bool dirty;

    // shared app model (example)
    struct {
        // add your items model here
        uint16_t item_count;
    } model;

    // per-screen states (allocated statically)
    struct {
        // HOME
        struct { uint8_t sel; } home;
        // MENU
        struct { uint8_t sel; uint8_t scroll; } menu;
        // ITEMS
        struct { uint8_t sel; uint8_t scroll; } items;
        // ADD_TYPE
        struct { uint8_t sel; } add_type;
        // ADD_EDIT
        struct { uint8_t field; int32_t value; } add_edit;
        // DIALOG
        struct { const char *title; const char *msg; uint8_t sel; } dialog;
    
    struct { bool waiting; bool done; bool timeout; ir_nec_code_t last; uint8_t slot_idx; } ir_learn;
    struct { uint8_t sel; uint8_t scroll; } ir_list;
    
    } st;



// inside ui_ctx_t
ui_ir_banner_t ir_banner;

} ui_ctx_t;

// init screens + nav stack
void ui_nav_init(ui_ctx_t *ctx, void *u8g2_ptr);

// stack ops
bool ui_nav_push(ui_ctx_t *ctx, ui_screen_id_t id, void *param);
bool ui_nav_pop(ui_ctx_t *ctx);
bool ui_nav_replace(ui_ctx_t *ctx, ui_screen_id_t id, void *param);
ui_screen_t *ui_nav_current(ui_ctx_t *ctx);

// dispatch + render
void ui_nav_dispatch(ui_ctx_t *ctx, const ui_event_t *ev);
void ui_nav_render_if_dirty(ui_ctx_t *ctx);

#ifdef __cplusplus
}
#endif
