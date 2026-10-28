#include "ui_internal.h"

static void home_neighbor(u8g2_t *u8, unsigned index, int y)
{
    remote_t remote;
    if (app_remote_get(index, &remote)) {
        ui_draw_icon(u8, 1, y, remote.icon);
        char short_name[10];
        snprintf(short_name, sizeof(short_name), "%.9s", remote.name);
        u8g2_SetFont(u8, u8g2_font_4x6_tf);
        u8g2_DrawStr(u8, 20, y + 7, short_name);
        u8g2_DrawStr(u8, 20, y + 14, remote_transport_name(remote.transport));
    } else {
        ui_plus_icon(u8, 1, y);
        u8g2_SetFont(u8, u8g2_font_4x6_tf);
        u8g2_DrawStr(u8, 20, y + 10, "Add remote");
    }
}

void ui_home_render(ui_ctx_t *ctx, u8g2_t *u8)
{
    unsigned count = app_remote_count();
    if (ctx->home_sel > count) ctx->home_sel = count;
    char counter[24];
    snprintf(counter, sizeof(counter), "%u/%u", ctx->home_sel + 1, count + 1);
    ui_title(u8, "REMOTES", counter);
    if (ctx->home_sel > 0) home_neighbor(u8, ctx->home_sel - 1, 18);
    else {
        u8g2_SetFont(u8, u8g2_font_5x8_tf);
        ui_center(u8, 29, app_get_ctx()->storage_error != ESP_OK ? "Storage error" : "BROWSE MODE");
    }
    u8g2_DrawBox(u8, 1, 38, 62, 51);
    u8g2_SetDrawColor(u8, 0);
    remote_t remote;
    if (app_remote_get(ctx->home_sel, &remote)) {
        ui_draw_icon(u8, 24, 42, remote.icon);
        u8g2_SetFont(u8, u8g2_font_6x10_tf);
        ui_center(u8, 73, remote.name);
        u8g2_SetFont(u8, u8g2_font_5x8_tf);
        ui_center(u8, 85, remote_transport_name(remote.transport));
    } else {
        ui_plus_icon(u8, 24, 42);
        u8g2_SetFont(u8, u8g2_font_6x10_tf);
        ui_center(u8, 73, "Add remote");
        u8g2_SetFont(u8, u8g2_font_5x8_tf);
        ui_center(u8, 85, count >= MAX_REMOTES ? "Storage full" : "NEW DEVICE");
    }
    u8g2_SetDrawColor(u8, 1);
    if (ctx->home_sel < count) home_neighbor(u8, ctx->home_sel + 1, 93);
    ui_footer(ctx, u8, ctx->home_sel < count ? "OK:open" : "OK:add",
           "C6:settings");
}

void ui_home_handle(ui_ctx_t *ctx, const ui_event_t *event)
{
    if (event->type == UI_EV_KEY && event->src == UI_SRC_KEYPAD
        && event->value == REMOTE_KEY_CUSTOM6 && !event->repeat) {
        ctx->settings_sel = 0;
        ui_nav_push(ctx, UI_SCREEN_SETTINGS, NULL);
        return;
    }
    unsigned count = app_remote_count();
    if (event->type == UI_EV_UP && ctx->home_sel) --ctx->home_sel;
    if (event->type == UI_EV_DOWN && ctx->home_sel < count) ++ctx->home_sel;
    if (event->type == UI_EV_MODE && ctx->home_sel < count) {
        remote_t remote;
        if (!app_remote_get(ctx->home_sel, &remote)) return;
        if (remote.transport != REMOTE_IR) { ui_notice(ctx, "OK:open first"); return; }
        ctx->active_remote = ctx->home_sel;
        ui_enter_remote_mode(ctx);
    }
    if (event->type != UI_EV_OK) return;
    if (ctx->home_sel == count) {
        if (count >= MAX_REMOTES) { ui_notice(ctx, "Storage full"); return; }
        if (app_get_ctx()->storage_error != ESP_OK) { ui_notice(ctx, "Storage error"); return; }
        memset(&ctx->draft, 0, sizeof(ctx->draft));
        ctx->editing = false;
        ctx->type_sel = 0;
        ctx->keyboard_sel = 0;
        ctx->keyboard_page = 0;
        ctx->icon_sel = 0;
        ui_nav_push(ctx, UI_SCREEN_ADD_TYPE, NULL);
    } else {
        ctx->active_remote = ctx->home_sel;
        ctx->menu_sel = 0;
        remote_t remote;
        if (!app_remote_get(ctx->active_remote, &remote)) return;
        if (remote.transport == REMOTE_BT && remote.bt.kind <= BT_REMOTE_MOUSE) {
            if (ui_open_bt(ctx, &remote)) {
                ctx->bt_menu_sel = 0;
                ui_nav_push(ctx, UI_SCREEN_BT_DEVICE, NULL);
            }
        } else ui_nav_push(ctx, UI_SCREEN_EDIT, NULL);
    }
}
