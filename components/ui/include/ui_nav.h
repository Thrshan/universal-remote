#pragma once
#include "ui_screen.h"
#include "app_state.h"
#include "bt_keyboard.h"
#define UI_STACK_MAX 8
#define UI_WIDTH 64
#define UI_HEIGHT 128

typedef enum { UI_IR_IDLE, UI_IR_WAITING, UI_IR_SAVED, UI_IR_TIMEOUT,
               UI_IR_FAILED, UI_IR_UNSUPPORTED, UI_IR_SAVE_FAILED } ui_ir_status_t;
typedef struct { ui_screen_id_t id; } ui_stack_entry_t;
typedef struct ui_ctx {
    void *u8g2;
    ui_screen_t screens[UI_SCREEN_COUNT];
    ui_stack_entry_t stack[UI_STACK_MAX];
    int top;
    bool dirty;
    int back_target_top; // Parent of the screen where an IR BACK hold began.
    uint32_t splash_started;
    unsigned home_sel;
    unsigned settings_sel;
    app_storage_usage_t storage_usage;
    esp_err_t storage_status;
    bool storage_pending;
    unsigned active_remote;
    unsigned menu_sel;
    unsigned type_sel;
    unsigned bt_type_sel;
    unsigned bt_menu_sel;
    unsigned bt_key_sel;
    unsigned bt_preset_sel;
    unsigned bt_speed_draft;
    unsigned bt_binding_sel;
    uint8_t bt_modifiers;
    char bt_text_draft[BT_TEXT_MAX + 1];
    unsigned bt_confirm_action;
    uint32_t bt_session;
    bt_snapshot_t bt_status;
    bool bt_power_pending;
    bool bt_remove_pending;
    unsigned keyboard_sel;
    unsigned keyboard_page;
    unsigned icon_sel;
    bool editing;
    remote_t draft;
    char notice[20];
    uint32_t notice_until;
    uint32_t sequence;
    unsigned last_key;
    uint32_t tx_request;
    char tx_status[20];
    struct {
        unsigned key;
        uint32_t request;
        ir_nec_code_t code;
        ui_ir_status_t status;
    } ir;
} ui_ctx_t;

void ui_nav_init(ui_ctx_t *ctx, void *display);
bool ui_nav_push(ui_ctx_t *ctx, ui_screen_id_t id, void *param);
bool ui_nav_pop(ui_ctx_t *ctx);
bool ui_nav_replace(ui_ctx_t *ctx, ui_screen_id_t id, void *param);
ui_screen_t *ui_nav_current(ui_ctx_t *ctx);
void ui_nav_home(ui_ctx_t *ctx);
void ui_nav_dispatch(ui_ctx_t *ctx, const ui_event_t *event);
void ui_nav_render_if_dirty(ui_ctx_t *ctx);
void ui_handle_app_event(ui_ctx_t *ctx, const app_evt_t *event);
void ui_cancel_learning(ui_ctx_t *ctx);
