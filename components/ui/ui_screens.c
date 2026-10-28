#include "ui_internal.h"

static void screen_enter(ui_ctx_t *ctx, void *param)
{
    (void)param;
    if (ui_screen_id(ctx) == UI_SCREEN_SPLASH) ctx->splash_started = ui_now_ms();
    if (ui_screen_id(ctx) == UI_SCREEN_STORAGE) {
        ctx->storage_pending = true;
        ctx->storage_status = ESP_OK;
        memset(&ctx->storage_usage, 0, sizeof(ctx->storage_usage));
    }
}

static void screen_exit(ui_ctx_t *ctx)
{
    if (ui_screen_id(ctx) == UI_SCREEN_REMOTE && ctx->bt_session) bt_runtime_release(ctx->bt_session);
    if (ui_screen_id(ctx) == UI_SCREEN_IR_WAIT) ui_cancel_learning(ctx);
    if (ui_screen_id(ctx) == UI_SCREEN_BT_DEVICE && ctx->bt_session) {
        bt_runtime_close();
        ctx->bt_session = 0;
        ctx->bt_power_pending = ctx->bt_remove_pending = false;
        ctx->tx_request = 0;
    }
}

static void screen_handle(ui_ctx_t *ctx, const ui_event_t *event)
{
    if (event->type == UI_EV_TICK) {
        // Let the initial Storage/Reading frame reach the OLED before scanning
        // NVS. Cache this snapshot until the page is reopened or refreshed.
        if (ui_screen_id(ctx) == UI_SCREEN_STORAGE && ctx->storage_pending && !ctx->dirty) {
            ctx->storage_status = app_storage_get_usage(&ctx->storage_usage);
            ctx->storage_pending = false;
            ctx->dirty = true;
        }
        if (ctx->bt_session) {
            bt_snapshot_t snapshot = bt_runtime_snapshot();
            if (snapshot.session == ctx->bt_session && (snapshot.state != ctx->bt_status.state
                || snapshot.error != ctx->bt_status.error || snapshot.step != ctx->bt_status.step || snapshot.mouse_movement != ctx->bt_status.mouse_movement
                || snapshot.mouse_buttons != ctx->bt_status.mouse_buttons)) {
                ctx->bt_status = snapshot;
                if (snapshot.state == BT_CONNECTED && !ctx->tx_request)
                    snprintf(ctx->tx_status, sizeof(ctx->tx_status), "Ready");
                if (snapshot.state == BT_ERROR) ctx->bt_remove_pending = false;
                ctx->dirty = true;
            }
        }
        if (ctx->notice[0] && (int32_t)(event->ts_ms - ctx->notice_until) >= 0) {
            ctx->notice[0] = 0;
            ctx->dirty = true;
        }
        if (ui_screen_id(ctx) == UI_SCREEN_SPLASH && (uint32_t)(event->ts_ms - ctx->splash_started) >= 2000)
            ui_nav_replace(ctx, UI_SCREEN_HOME, NULL);
        return;
    }
    switch (ui_screen_id(ctx)) {
        case UI_SCREEN_HOME: ui_home_handle(ctx, event); break;
        case UI_SCREEN_SETTINGS: ui_settings_handle(ctx, event); break;
        case UI_SCREEN_ABOUT:
            if (event->type == UI_EV_BACK) ui_nav_pop(ctx);
            break;
        case UI_SCREEN_STORAGE: ui_storage_handle(ctx, event); break;
        case UI_SCREEN_REMOTE: ui_remote_handle(ctx, event); break;
        case UI_SCREEN_EDIT: ui_edit_handle(ctx, event); break;
        case UI_SCREEN_ADD_TYPE: ui_type_handle(ctx, event); break;
        case UI_SCREEN_BT_TYPE: ui_bt_type_handle(ctx, event); break;
        case UI_SCREEN_BT_DEVICE: ui_bt_device_handle(ctx, event); break;
        case UI_SCREEN_BT_CONFIRM: ui_bt_confirm_handle(ctx, event); break;
        case UI_SCREEN_BT_KEYS: ui_bt_keys_handle(ctx, event); break;
        case UI_SCREEN_BT_PRESET: ui_bt_preset_handle(ctx, event); break;
        case UI_SCREEN_BT_BINDING: ui_bt_binding_handle(ctx, event); break;
        case UI_SCREEN_BT_TEXT: ui_bt_text_handle(ctx, event); break;
        case UI_SCREEN_BT_SPEED: ui_bt_speed_handle(ctx, event); break;
        case UI_SCREEN_NAME: ui_name_handle(ctx, event); break;
        case UI_SCREEN_ICON: ui_icon_handle(ctx, event); break;
        case UI_SCREEN_IR_KEYS: ui_keys_handle(ctx, event); break;
        case UI_SCREEN_IR_REPLACE:
            if (event->type == UI_EV_OK) ui_learn_start(ctx, true);
            break;
        case UI_SCREEN_IR_WAIT: ui_wait_handle(ctx, event); break;
        case UI_SCREEN_IR_RESULT: ui_result_handle(ctx, event); break;
        case UI_SCREEN_REMOVE: ui_remove_handle(ctx, event); break;
        case UI_SCREEN_CONSTRUCTION:
            if (event->type == UI_EV_OK || event->type == UI_EV_BACK) ui_nav_home(ctx);
            break;
        default: break;
    }
}

static void screen_render(ui_ctx_t *ctx, u8g2_t *u8)
{
    switch (ui_screen_id(ctx)) {
        case UI_SCREEN_SPLASH:
            u8g2_SetFont(u8, u8g2_font_6x10_tf);
            ui_center(u8, 59, "UNIVERSAL");
            ui_center(u8, 74, "REMOTE");
            break;
        case UI_SCREEN_HOME: ui_home_render(ctx, u8); break;
        case UI_SCREEN_SETTINGS: ui_settings_render(ctx, u8); break;
        case UI_SCREEN_ABOUT: ui_about_render(ctx, u8); break;
        case UI_SCREEN_STORAGE: ui_storage_render(ctx, u8); break;
        case UI_SCREEN_REMOTE: ui_remote_render(ctx, u8); break;
        case UI_SCREEN_EDIT: ui_edit_render(ctx, u8); break;
        case UI_SCREEN_ADD_TYPE: ui_type_render(ctx, u8); break;
        case UI_SCREEN_BT_TYPE: ui_bt_type_render(ctx, u8); break;
        case UI_SCREEN_BT_DEVICE: ui_bt_device_render(ctx, u8); break;
        case UI_SCREEN_BT_CONFIRM: ui_bt_confirm_render(ctx, u8); break;
        case UI_SCREEN_BT_KEYS: ui_bt_keys_render(ctx, u8); break;
        case UI_SCREEN_BT_PRESET: ui_bt_preset_render(ctx, u8); break;
        case UI_SCREEN_BT_BINDING: ui_bt_binding_render(ctx, u8); break;
        case UI_SCREEN_BT_TEXT: ui_name_render(ctx, u8); break;
        case UI_SCREEN_BT_SPEED: ui_bt_speed_render(ctx, u8); break;
        case UI_SCREEN_NAME: ui_name_render(ctx, u8); break;
        case UI_SCREEN_ICON: ui_icon_render(ctx, u8); break;
        case UI_SCREEN_IR_KEYS: ui_keys_render(ctx, u8); break;
        case UI_SCREEN_IR_REPLACE: ui_replace_render(ctx, u8); break;
        case UI_SCREEN_IR_WAIT: ui_wait_render(ctx, u8); break;
        case UI_SCREEN_IR_RESULT: ui_result_render(ctx, u8); break;
        case UI_SCREEN_REMOVE: ui_remove_render(ctx, u8); break;
        case UI_SCREEN_CONSTRUCTION: ui_construction_render(ctx, u8); break;
        default: break;
    }
}

void ui_handle_app_event(ui_ctx_t *ctx, const app_evt_t *event)
{
    if (event->type == APP_EVT_BT_PEER) {
        if (!ctx->bt_session || event->request_id != ctx->bt_session) return;
        remote_t remote;
        if (!app_remote_get(ctx->active_remote, &remote) || remote.transport != REMOTE_BT) return;
        remote.bt.bonded = event->bt.bonded;
        remote.bt.addr_type = event->bt.addr_type;
        memcpy(remote.bt.peer, event->bt.peer, 6);
        if (!event->bt.bonded) remote.bt.bond_store = BT_BOND_STORE_PROFILE;
        bool saved = app_remote_update(ctx->active_remote, &remote) == ESP_OK;
        bt_runtime_peer_saved(ctx->bt_session, saved, &remote);
        if (!saved) { ctx->bt_remove_pending = false; ui_notice(ctx, "Save failed"); }
        else if (!event->bt.bonded && ctx->bt_remove_pending) {
            ctx->bt_remove_pending = false;
            if (app_remote_remove(ctx->active_remote) == ESP_OK) ui_nav_home(ctx);
            else ui_notice(ctx, "Delete failed");
        }
        ctx->dirty = true;
        return;
    }
    if (ctx->bt_session && ui_screen_id(ctx) == UI_SCREEN_REMOTE && event->request_id
        && event->request_id == ctx->tx_request
        && (event->type == APP_EVT_BT_TX_DONE || event->type == APP_EVT_BT_TX_FAIL)) {
        snprintf(ctx->tx_status, sizeof(ctx->tx_status), "%s", event->type == APP_EVT_BT_TX_DONE ? "Sent" : "Send failed");
        ctx->dirty = true;
    }
    if (event->request_id && event->request_id == ctx->ir.request && ui_screen_id(ctx) == UI_SCREEN_IR_WAIT) {
        if (event->type == APP_EVT_IR_UNSUPPORTED) {
            ctx->ir.status = UI_IR_UNSUPPORTED;
        } else if (event->type == APP_EVT_IR_LEARNED) {
            ctx->ir.code = event->ir.code;
            ctx->ir.request = 0;
            ctx->ir.status = app_remote_set_code(ctx->active_remote, ctx->ir.key, event->ir.code) == ESP_OK
                ? UI_IR_SAVED : UI_IR_SAVE_FAILED;
            ui_nav_replace(ctx, UI_SCREEN_IR_RESULT, NULL);
        } else if (event->type == APP_EVT_IR_LEARN_TIMEOUT || event->type == APP_EVT_IR_LEARN_FAILED) {
            ctx->ir.request = 0;
            ctx->ir.status = event->type == APP_EVT_IR_LEARN_TIMEOUT ? UI_IR_TIMEOUT : UI_IR_FAILED;
        }
        ctx->dirty = true;
    }
    if (event->request_id && event->request_id == ctx->tx_request
        && (event->type == APP_EVT_IR_TX_DONE || event->type == APP_EVT_IR_TX_FAIL)) {
        snprintf(ctx->tx_status, sizeof(ctx->tx_status), "%s", event->type == APP_EVT_IR_TX_DONE ? "Sent" : "Send failed");
        ctx->dirty = true;
    }
}

void ui_register_screens(ui_ctx_t *ctx)
{
    for (unsigned i = 0; i < UI_SCREEN_COUNT; ++i)
        ctx->screens[i] = (ui_screen_t){.id = i, .on_enter = screen_enter,
            .on_exit = screen_exit, .handle = screen_handle, .render = screen_render};
}
