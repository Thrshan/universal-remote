#include "ui_internal.h"

void ui_edit_handle(ui_ctx_t *ctx, const ui_event_t *event)
{
    if (event->type == UI_EV_MODE && ctx->top > 0) {
        remote_t remote;
        if (app_remote_get(ctx->active_remote, &remote) && (remote.transport == REMOTE_IR
            || (remote.transport == REMOTE_BT && ctx->bt_session))) ui_enter_remote_mode(ctx);
        return;
    }
    if (event->type == UI_EV_UP && ctx->menu_sel) --ctx->menu_sel;
    if (event->type == UI_EV_DOWN && ctx->menu_sel < 3) ++ctx->menu_sel;
    if (event->type == UI_EV_BACK) { ui_nav_pop(ctx); return; }
    if (event->type != UI_EV_OK) return;
    if (!app_remote_get(ctx->active_remote, &ctx->draft)) return;
    ctx->editing = true;
    switch (ctx->menu_sel) {
        case 0:
            ui_nav_push(ctx, ctx->draft.transport == REMOTE_IR ? UI_SCREEN_IR_KEYS
                : ctx->draft.transport == REMOTE_BT ? UI_SCREEN_BT_KEYS : UI_SCREEN_CONSTRUCTION, NULL);
            ctx->bt_key_sel = 1;
            break;
        case 1:
            ctx->keyboard_sel = 0;
            ctx->keyboard_page = 0;
            ui_nav_push(ctx, UI_SCREEN_NAME, NULL);
            break;
        case 2:
            ctx->icon_sel = ctx->draft.icon;
            ui_nav_push(ctx, UI_SCREEN_ICON, NULL);
            break;
        case 3:
            ctx->menu_sel = 0;
            ui_nav_push(ctx, UI_SCREEN_REMOVE, NULL);
            break;
    }
}

void ui_edit_render(ui_ctx_t *ctx, u8g2_t *u8)
{
    remote_t remote;
    if (!app_remote_get(ctx->active_remote, &remote)) return;
    ui_title(u8, "Edit remote", NULL);
    u8g2_SetFont(u8, u8g2_font_6x10_tf);
    ui_center(u8, 29, remote.name);
    const char *items[] = {remote.transport == REMOTE_IR ? "Learn keys" : remote.transport == REMOTE_BT ? "Key map" : "Setup",
                           "Name", "Icon", "Remove"};
    for (unsigned i = 0; i < 4; ++i) ui_row(u8, 37 + i * 18, items[i], ctx->menu_sel == i);
    ui_footer(ctx, u8, "OK:select", remote.transport == REMOTE_WIFI ? "BACK:browse" : "MODE:use");
}

void ui_remove_handle(ui_ctx_t *ctx, const ui_event_t *event)
{
    if (event->type == UI_EV_UP || event->type == UI_EV_DOWN) ctx->menu_sel ^= 1;
    if (event->type == UI_EV_BACK) { ui_nav_pop(ctx); ctx->menu_sel = 3; return; }
    if (event->type != UI_EV_OK) return;
    if (!ctx->menu_sel) { ui_nav_pop(ctx); ctx->menu_sel = 3; return; }
    remote_t remote;
    if (app_remote_get(ctx->active_remote, &remote) && remote.transport == REMOTE_BT && ctx->bt_session) {
        if (ctx->bt_remove_pending) return;
        if (bt_runtime_forget(ctx->bt_session, false)) {
            ctx->bt_remove_pending = true;
            ui_notice(ctx, "Unpairing...");
        } else ui_notice(ctx, "BT busy");
        return;
    }
    if (app_remote_remove(ctx->active_remote) == ESP_OK) ui_nav_home(ctx);
    else ui_notice(ctx, "Delete failed");
}

void ui_remove_render(ui_ctx_t *ctx, u8g2_t *u8)
{
    ui_title(u8, "Remove?", NULL);
    remote_t remote;
    if (!app_remote_get(ctx->active_remote, &remote)) return;
    u8g2_SetFont(u8, u8g2_font_6x10_tf);
    ui_center(u8, 34, remote.name);
    u8g2_SetFont(u8, u8g2_font_5x8_tf);
    ui_center(u8, 48, remote.transport == REMOTE_BT ? "Unpair and" : "Erase remote");
    ui_center(u8, 57, remote.transport == REMOTE_BT ? "erase remote?" : "and keys?");
    ui_row(u8, 70, "Cancel", ctx->menu_sel == 0);
    ui_row(u8, 90, "Remove", ctx->menu_sel == 1);
    ui_footer(ctx, u8, "OK:confirm", "BACK:cancel");
}

void ui_construction_render(ui_ctx_t *ctx, u8g2_t *u8)
{
    remote_t remote;
    if (!app_remote_get(ctx->active_remote, &remote)) return;
    ui_title(u8, "Setup", remote_transport_name(remote.transport));
    ui_draw_icon(u8, 24, 27, remote.icon);
    u8g2_SetFont(u8, u8g2_font_6x10_tf);
    ui_center(u8, 61, "Under");
    ui_center(u8, 73, "Construction");
    ui_row(u8, 91, "Exit", true);
    ui_footer(ctx, u8, "Remote saved", "OK:exit");
}
