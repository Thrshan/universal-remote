#include "ui_internal.h"

void ui_cancel_learning(ui_ctx_t *ctx)
{
    if (!ctx->ir.request) return;
    app_cmd_t command = {.type = APP_CMD_IR_LEARN_CANCEL, .request_id = ctx->ir.request};
    (void)app_send_cmd(&command, pdMS_TO_TICKS(50));
    ctx->ir.request = 0; // Late results can never attach to another key.
}

void ui_learn_start(ui_ctx_t *ctx, bool replace)
{
    bool same_screen = ui_screen_id(ctx) == UI_SCREEN_IR_WAIT;
    if (same_screen) ui_cancel_learning(ctx);
    ctx->ir.status = UI_IR_WAITING;
    ctx->ir.request = ++ctx->sequence;
    app_cmd_t command = {.type = APP_CMD_IR_LEARN_ONESHOT, .request_id = ctx->ir.request};
    command.learn.timeout_ms = 15000;
    if (!app_send_cmd(&command, 0)) {
        ctx->ir.request = 0;
        ctx->ir.status = UI_IR_FAILED;
    }
    if (!same_screen) {
        if (replace) ui_nav_replace(ctx, UI_SCREEN_IR_WAIT, NULL);
        else ui_nav_push(ctx, UI_SCREEN_IR_WAIT, NULL);
    }
    ctx->dirty = true;
}

void ui_keys_handle(ui_ctx_t *ctx, const ui_event_t *event)
{
    if (event->src != UI_SRC_KEYPAD || event->repeat || event->type == UI_EV_OK_LONG
        || event->type == UI_EV_BACK_LONG || event->type == UI_EV_HOME_LONG) return;
    unsigned key = event->value;
    remote_t remote;
    if (!remote_key_assignable(key) || !app_remote_get(ctx->active_remote, &remote)) return;
    ctx->ir.key = key;
    ctx->ir.code = remote.keys[key - 1];
    if (remote.assigned & (1u << (key - 1))) ui_nav_push(ctx, UI_SCREEN_IR_REPLACE, NULL);
    else ui_learn_start(ctx, false);
}

void ui_keys_render(ui_ctx_t *ctx, u8g2_t *u8)
{
    remote_t remote;
    if (!app_remote_get(ctx->active_remote, &remote)) return;
    ui_title(u8, "Learn keys", "IR");
    u8g2_SetFont(u8, u8g2_font_6x10_tf);
    ui_center(u8, 30, remote.name);
    ui_draw_icon(u8, 24, 37, remote.icon);
    ui_center(u8, 68, "Press key");
    ui_center(u8, 80, "to learn IR");
    char count[16];
    snprintf(count, sizeof(count), "%u/20 saved", remote_assigned_count(&remote));
    u8g2_SetFont(u8, u8g2_font_5x8_tf);
    ui_center(u8, 99, count);
    ui_footer(ctx, u8, "Hold BACK:back", "Hold HOME:exit");
}

static void code_render(u8g2_t *u8, ir_nec_code_t code, int y)
{
    char line[16];
    u8g2_SetFont(u8, u8g2_font_6x10_tf);
    ui_center(u8, y, "Protocol NEC");
    snprintf(line, sizeof(line), "Addr %04X", code.addr);
    ui_center(u8, y + 12, line);
    snprintf(line, sizeof(line), "Cmd  %04X", code.cmd);
    ui_center(u8, y + 24, line);
}

void ui_replace_render(ui_ctx_t *ctx, u8g2_t *u8)
{
    ui_title(u8, "Key saved", NULL);
    u8g2_SetFont(u8, u8g2_font_6x10_tf);
    ui_center(u8, 30, remote_key_name(ctx->ir.key));
    code_render(u8, ctx->ir.code, 45);
    ui_row(u8, 88, "Re-record", true);
    ui_footer(ctx, u8, "OK:re-record", "Hold BACK");
}

void ui_wait_handle(ui_ctx_t *ctx, const ui_event_t *event)
{
    if (event->repeat) return;
    if (event->type == UI_EV_OK && (ctx->ir.status == UI_IR_FAILED || ctx->ir.status == UI_IR_TIMEOUT)) {
        ui_learn_start(ctx, false);
    }
}

void ui_wait_render(ui_ctx_t *ctx, u8g2_t *u8)
{
    ui_title(u8, "Learn IR", NULL);
    u8g2_SetFont(u8, u8g2_font_6x10_tf);
    ui_center(u8, 30, remote_key_name(ctx->ir.key));
    bool retry = ctx->ir.status == UI_IR_TIMEOUT || ctx->ir.status == UI_IR_FAILED;
    if (retry) {
        ui_center(u8, 50, ctx->ir.status == UI_IR_TIMEOUT ? "Timed out" : "Read error");
        ui_center(u8, 65, "No IR saved");
        u8g2_SetFont(u8, u8g2_font_5x8_tf);
        ui_center(u8, 96, "OK to retry");
    } else {
        ui_center(u8, 49, ctx->ir.status == UI_IR_UNSUPPORTED ? "NEC only" : "Waiting");
        ui_center(u8, 61, "for IR");
        ui_center(u8, 73, "command");
        u8g2_SetFont(u8, u8g2_font_5x8_tf);
        ui_center(u8, 92, "Press source");
        ui_center(u8, 104, "remote key");
    }
    ui_footer(ctx, u8, "Hold BACK", "to cancel");
}

void ui_result_handle(ui_ctx_t *ctx, const ui_event_t *event)
{
    if (event->type == UI_EV_OK) {
        if (ctx->ir.status == UI_IR_SAVE_FAILED) {
            if (app_remote_set_code(ctx->active_remote, ctx->ir.key, ctx->ir.code) == ESP_OK)
                ctx->ir.status = UI_IR_SAVED;
            else ui_notice(ctx, "Save failed");
        } else ui_nav_pop(ctx);
    }
}

void ui_result_render(ui_ctx_t *ctx, u8g2_t *u8)
{
    ui_title(u8, ctx->ir.status == UI_IR_SAVED ? "Saved" : "Save failed", NULL);
    u8g2_SetFont(u8, u8g2_font_6x10_tf);
    ui_center(u8, 30, remote_key_name(ctx->ir.key));
    code_render(u8, ctx->ir.code, 48);
    ui_row(u8, 90, ctx->ir.status == UI_IR_SAVED ? "Next key" : "Retry save", true);
    ui_footer(ctx, u8, ctx->ir.status == UI_IR_SAVED ? "OK:next key" : "OK:retry", "Hold BACK");
}
