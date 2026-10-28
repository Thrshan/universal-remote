#include "ui_internal.h"

void ui_enter_remote_mode(ui_ctx_t *ctx)
{
    ctx->last_key = 0;
    ctx->tx_request = 0;
    ctx->tx_status[0] = 0;
    ctx->bt_power_pending = false;
    ui_nav_push(ctx, UI_SCREEN_REMOTE, NULL);
}

bool ui_save_mouse_speed(ui_ctx_t *ctx, remote_t *remote, unsigned speed)
{
    remote->bt.mouse_speed = speed;
    if (app_remote_update(ctx->active_remote, remote) != ESP_OK) {
        ui_notice(ctx, "Save failed"); return false;
    }
    return true;
}

static void mouse_handle(ui_ctx_t *ctx, const remote_t *remote, const ui_event_t *event)
{
    unsigned key = event->value;
    if (!(bt_mouse_mask(remote->bt.mouse_mode) & (1u << (key - 1)))) {
        if (event->type != UI_EV_KEY_RELEASE) snprintf(ctx->tx_status, sizeof(ctx->tx_status), "Unassigned");
        return;
    }
    bool patterns = remote->bt.mouse_mode == BT_MOUSE_PATTERNS;
    unsigned faster = patterns ? REMOTE_KEY_VOL_UP : REMOTE_KEY_CUSTOM4;
    unsigned slower = patterns ? REMOTE_KEY_VOL_DOWN : REMOTE_KEY_CUSTOM5;
    if (key == faster || key == slower) {
        if (event->type == UI_EV_KEY_RELEASE || event->type == UI_EV_KEY_LONG) return;
        remote_t updated = *remote;
        unsigned speed = bt_mouse_speed(remote);
        if (key == faster && speed < BT_MOUSE_SPEED_MAX) ++speed;
        if (key == slower && speed > 1) --speed;
        if (speed != bt_mouse_speed(remote) && !ui_save_mouse_speed(ctx, &updated, speed)) return;
        snprintf(ctx->tx_status, sizeof(ctx->tx_status), "Speed %u/10", speed);
        return;
    }
    bt_mouse_input_t input = event->type == UI_EV_KEY_RELEASE ? BT_MOUSE_RELEASE
        : event->type == UI_EV_OK_LONG || event->type == UI_EV_KEY_LONG ? BT_MOUSE_HOLD
        : event->repeat ? BT_MOUSE_REPEAT : BT_MOUSE_PRESS;
    ctx->last_key = key;
    if (!bt_runtime_mouse_input(ctx->bt_session, key, input, event->ts_ms))
        snprintf(ctx->tx_status, sizeof(ctx->tx_status), "BT offline");
    else snprintf(ctx->tx_status, sizeof(ctx->tx_status), "%s", bt_mouse_key_label(remote->bt.mouse_mode, key));
}

void ui_remote_handle(ui_ctx_t *ctx, const ui_event_t *event)
{
    if (event->type == UI_EV_MODE) {
        ctx->bt_power_pending = false;
        ctx->tx_request = 0;
        ui_nav_pop(ctx); return;
    }
    if (event->type == UI_EV_BACK_LONG || event->type == UI_EV_HOME_LONG || event->src != UI_SRC_KEYPAD) return;
    unsigned key = event->value;
    remote_t remote;
    if (!remote_key_assignable(key) || !app_remote_get(ctx->active_remote, &remote)) return;
    if (remote.transport == REMOTE_BT && remote.bt.kind == BT_REMOTE_MOUSE) {
        if (event->type != UI_EV_POWER_LONG) mouse_handle(ctx, &remote, event);
        return;
    }
    if (event->type == UI_EV_OK_LONG || event->type == UI_EV_KEY_LONG) return;
    if (remote.transport == REMOTE_BT && remote.bt.kind == BT_REMOTE_KEYBOARD) {
        bool fire_power = remote.bt.preset == BT_PRESET_FIRE_TV && key == REMOTE_KEY_POWER;
        bool long_power = event->type == UI_EV_POWER_LONG && key == REMOTE_KEY_POWER;
        if (event->type == UI_EV_KEY_RELEASE) {
            if (!fire_power || !ctx->bt_power_pending) return;
            ctx->bt_power_pending = false;
        } else if (fire_power && !long_power) {
            if (!event->repeat) ctx->bt_power_pending = true;
            return; // Short power is sent on release; a hold sends only Ctrl+Alt+Del.
        } else if (long_power) {
            if (!fire_power || !ctx->bt_power_pending) return;
            ctx->bt_power_pending = false;
        }
        ctx->last_key = key;
        if (!bt_profile_key_assigned(&remote, key)) {
            snprintf(ctx->tx_status, sizeof(ctx->tx_status), "Unassigned"); return;
        }
        // A held arrow/channel key must not repeatedly type an entire string.
        if (remote.bt.preset == BT_PRESET_TEXT && event->repeat) return;
        ctx->tx_request = ++ctx->sequence;
        if (bt_runtime_tap(ctx->bt_session, ctx->tx_request, key, long_power))
            snprintf(ctx->tx_status, sizeof(ctx->tx_status), "%s", long_power ? "CtrlAltDel" : "Sending...");
        else { ctx->tx_request = 0; snprintf(ctx->tx_status, sizeof(ctx->tx_status), "BT offline"); }
        return;
    }
    if (event->type == UI_EV_KEY_RELEASE || event->type == UI_EV_POWER_LONG) return;
    ctx->last_key = key;
    ctx->tx_request = 0;
    if (remote.transport != REMOTE_IR) {
        snprintf(ctx->tx_status, sizeof(ctx->tx_status), "Not ready");
    } else if (!(remote.assigned & (1u << (key - 1)))) {
        snprintf(ctx->tx_status, sizeof(ctx->tx_status), "Unassigned");
    } else {
        app_cmd_t command = {.type = APP_CMD_IR_TX_SEND, .request_id = ++ctx->sequence};
        command.tx.code = remote.keys[key - 1];
        if (app_send_cmd(&command, 0)) {
            ctx->tx_request = command.request_id;
            snprintf(ctx->tx_status, sizeof(ctx->tx_status), "Sending...");
        } else snprintf(ctx->tx_status, sizeof(ctx->tx_status), "Busy: retry");
    }
}

void ui_remote_render(ui_ctx_t *ctx, u8g2_t *u8)
{
    remote_t remote;
    if (!app_remote_get(ctx->active_remote, &remote)) return;
    ui_title(u8, "REMOTE", remote_transport_name(remote.transport));
    ui_draw_icon(u8, 24, 21, remote.icon);
    u8g2_SetFont(u8, u8g2_font_6x10_tf);
    ui_center(u8, 50, remote.name);
    char info[16];
    snprintf(info, sizeof(info), "%u/20 keys", remote_assigned_count(&remote));
    u8g2_SetFont(u8, u8g2_font_5x8_tf);
    ui_center(u8, 62, remote.transport == REMOTE_IR ? info : remote.transport == REMOTE_BT
        ? bt_runtime_status_name(ctx->bt_status.state) : "Not ready");
    u8g2_DrawFrame(u8, 1, 70, 62, 38);
    bool mouse = remote.transport == REMOTE_BT && remote.bt.kind == BT_REMOTE_MOUSE;
    char speed[20];
    snprintf(speed, sizeof(speed), "%s %u/10", ctx->bt_status.mouse_buttons & 1 ? "HOLD" : "Speed", bt_mouse_speed(&remote));
    ui_center(u8, 79, mouse ? speed : "LAST BUTTON");
    u8g2_SetFont(u8, u8g2_font_6x10_tf);
    ui_center(u8, 92, mouse && ctx->bt_status.mouse_movement ? remote_key_name(ctx->bt_status.mouse_movement)
        : ctx->last_key ? remote_key_name(ctx->last_key) : "Press key");
    u8g2_SetFont(u8, u8g2_font_5x8_tf);
    ui_center(u8, 103, mouse && ctx->bt_status.mouse_movement
        ? bt_mouse_key_label(remote.bt.mouse_mode, ctx->bt_status.mouse_movement) : ctx->tx_status);
    ui_footer(ctx, u8, "Press keys", ctx->bt_session ? "MODE:BT menu" : "MODE:browse");
}
