#include "ui_nav.h"
#include "ui_icons.h"
#include "u8g2.h"
#include "esp_timer.h"
#include <stdio.h>
#include <string.h>

static uint32_t now_ms(void) { return (uint32_t)(esp_timer_get_time() / 1000ULL); }
static ui_screen_id_t screen_id(ui_ctx_t *ctx) { return ui_nav_current(ctx)->id; }
static void notice(ui_ctx_t *ctx, const char *message)
{
    snprintf(ctx->notice, sizeof(ctx->notice), "%s", message);
    ctx->notice_until = now_ms() + 2500;
    ctx->dirty = true;
}
static void center(u8g2_t *u8, int y, const char *text)
{
    const uint8_t *font = u8->font;
    // Keep long names readable in full instead of clipping the larger font.
    if (u8g2_GetStrWidth(u8, text) > UI_WIDTH - 4) u8g2_SetFont(u8, u8g2_font_5x8_tf);
    if (u8g2_GetStrWidth(u8, text) > UI_WIDTH) u8g2_SetFont(u8, u8g2_font_4x6_tf);
    int width = u8g2_GetStrWidth(u8, text);
    u8g2_DrawStr(u8, width < UI_WIDTH ? (UI_WIDTH - width) / 2 : 0, y, text);
    u8g2_SetFont(u8, font);
}
static void title(u8g2_t *u8, const char *text, const char *right)
{
    u8g2_SetFont(u8, u8g2_font_5x8_tf);
    u8g2_DrawStr(u8, 2, 10, text);
    if (right) {
        u8g2_SetFont(u8, u8g2_font_4x6_tf);
        u8g2_DrawStr(u8, UI_WIDTH - u8g2_GetStrWidth(u8, right) - 1, 9, right);
    }
    u8g2_DrawHLine(u8, 0, 14, UI_WIDTH);
}
static void footer(ui_ctx_t *ctx, u8g2_t *u8, const char *first, const char *second)
{
    u8g2_DrawHLine(u8, 0, 111, UI_WIDTH);
    u8g2_SetFont(u8, u8g2_font_5x8_tf);
    center(u8, 119, ctx->notice[0] ? ctx->notice : first);
    center(u8, 126, second);
}
static void row(u8g2_t *u8, int y, const char *text, bool selected)
{
    if (selected) {
        u8g2_DrawBox(u8, 1, y, 62, 16);
        u8g2_SetDrawColor(u8, 0);
    }
    u8g2_SetFont(u8, u8g2_font_6x10_tf);
    if (u8g2_GetStrWidth(u8, text) > 56) u8g2_SetFont(u8, u8g2_font_5x8_tf);
    u8g2_DrawStr(u8, 4, y + 11, text);
    u8g2_SetDrawColor(u8, 1);
}
static void plus_icon(u8g2_t *u8, int x, int y)
{
    u8g2_DrawFrame(u8, x + 1, y + 1, 14, 14);
    u8g2_DrawHLine(u8, x + 4, y + 8, 8);
    u8g2_DrawVLine(u8, x + 8, y + 4, 8);
}
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
        plus_icon(u8, 1, y);
        u8g2_SetFont(u8, u8g2_font_4x6_tf);
        u8g2_DrawStr(u8, 20, y + 10, "Add remote");
    }
}
static void home_render(ui_ctx_t *ctx, u8g2_t *u8)
{
    unsigned count = app_remote_count();
    if (ctx->home_sel > count) ctx->home_sel = count;
    char counter[24];
    snprintf(counter, sizeof(counter), "%u/%u", ctx->home_sel + 1, count + 1);
    title(u8, "REMOTES", counter);
    if (ctx->home_sel > 0) home_neighbor(u8, ctx->home_sel - 1, 18);
    else {
        u8g2_SetFont(u8, u8g2_font_5x8_tf);
        center(u8, 29, app_get_ctx()->storage_error != ESP_OK ? "Storage error" : "BROWSE MODE");
    }
    u8g2_DrawBox(u8, 1, 38, 62, 51);
    u8g2_SetDrawColor(u8, 0);
    remote_t remote;
    if (app_remote_get(ctx->home_sel, &remote)) {
        ui_draw_icon(u8, 24, 42, remote.icon);
        u8g2_SetFont(u8, u8g2_font_6x10_tf);
        center(u8, 73, remote.name);
        u8g2_SetFont(u8, u8g2_font_5x8_tf);
        center(u8, 85, remote_transport_name(remote.transport));
    } else {
        plus_icon(u8, 24, 42);
        u8g2_SetFont(u8, u8g2_font_6x10_tf);
        center(u8, 73, "Add remote");
        u8g2_SetFont(u8, u8g2_font_5x8_tf);
        center(u8, 85, count >= MAX_REMOTES ? "Storage full" : "NEW DEVICE");
    }
    u8g2_SetDrawColor(u8, 1);
    if (ctx->home_sel < count) home_neighbor(u8, ctx->home_sel + 1, 93);
    footer(ctx, u8, ctx->home_sel < count ? "MODE:use" : "OK:add",
           ctx->home_sel < count ? "OK:edit" : "HOME:browse");
}
static bool custom_key(unsigned key)
{
    return key == REMOTE_KEY_CUSTOM2 || key == REMOTE_KEY_CUSTOM3
        || key == REMOTE_KEY_CUSTOM4 || key == REMOTE_KEY_CUSTOM5 || key == REMOTE_KEY_CUSTOM6;
}
static void home_handle(ui_ctx_t *ctx, const ui_event_t *event)
{
    unsigned count = app_remote_count();
    if (event->type == UI_EV_UP && ctx->home_sel) --ctx->home_sel;
    if (event->type == UI_EV_DOWN && ctx->home_sel < count) ++ctx->home_sel;
    if (event->type == UI_EV_MODE && ctx->home_sel < count) {
        ctx->active_remote = ctx->home_sel;
        ctx->last_key = 0;
        ctx->tx_status[0] = 0;
        ui_nav_push(ctx, UI_SCREEN_REMOTE, NULL);
    }
    if (event->type != UI_EV_OK) return;
    if (ctx->home_sel == count) {
        if (count >= MAX_REMOTES) { notice(ctx, "Storage full"); return; }
        if (app_get_ctx()->storage_error != ESP_OK) { notice(ctx, "Storage error"); return; }
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
        ui_nav_push(ctx, UI_SCREEN_EDIT, NULL);
    }
}
static void remote_handle(ui_ctx_t *ctx, const ui_event_t *event)
{
    if (event->type == UI_EV_MODE) { ui_nav_pop(ctx); return; }
    if (event->type == UI_EV_OK_LONG || event->type == UI_EV_BACK_LONG
        || event->type == UI_EV_HOME_LONG || event->src != UI_SRC_KEYPAD) return;
    unsigned key = event->value;
    remote_t remote;
    if (!remote_key_assignable(key) || !app_remote_get(ctx->active_remote, &remote)) return;
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
static void remote_render(ui_ctx_t *ctx, u8g2_t *u8)
{
    remote_t remote;
    if (!app_remote_get(ctx->active_remote, &remote)) return;
    title(u8, "REMOTE", remote_transport_name(remote.transport));
    ui_draw_icon(u8, 24, 21, remote.icon);
    u8g2_SetFont(u8, u8g2_font_6x10_tf);
    center(u8, 50, remote.name);
    char info[16];
    snprintf(info, sizeof(info), "%u/20 keys", remote_assigned_count(&remote));
    u8g2_SetFont(u8, u8g2_font_5x8_tf);
    center(u8, 62, remote.transport == REMOTE_IR ? info : "Dummy transport");
    u8g2_DrawFrame(u8, 1, 70, 62, 38);
    center(u8, 79, "LAST BUTTON");
    u8g2_SetFont(u8, u8g2_font_6x10_tf);
    center(u8, 92, ctx->last_key ? remote_key_name(ctx->last_key) : "Press key");
    u8g2_SetFont(u8, u8g2_font_5x8_tf);
    center(u8, 103, ctx->tx_status);
    footer(ctx, u8, "Press keys", "MODE:browse");
}
static void edit_handle(ui_ctx_t *ctx, const ui_event_t *event)
{
    if (event->type == UI_EV_UP && ctx->menu_sel) --ctx->menu_sel;
    if (event->type == UI_EV_DOWN && ctx->menu_sel < 3) ++ctx->menu_sel;
    if (event->type == UI_EV_BACK) { ui_nav_pop(ctx); return; }
    if (event->type != UI_EV_OK) return;
    if (!app_remote_get(ctx->active_remote, &ctx->draft)) return;
    ctx->editing = true;
    switch (ctx->menu_sel) {
        case 0:
            ui_nav_push(ctx, ctx->draft.transport == REMOTE_IR ? UI_SCREEN_IR_KEYS : UI_SCREEN_CONSTRUCTION, NULL);
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
static void edit_render(ui_ctx_t *ctx, u8g2_t *u8)
{
    remote_t remote;
    if (!app_remote_get(ctx->active_remote, &remote)) return;
    title(u8, "Edit remote", NULL);
    u8g2_SetFont(u8, u8g2_font_6x10_tf);
    center(u8, 29, remote.name);
    const char *items[] = {remote.transport == REMOTE_IR ? "Learn keys" : "Setup",
                           "Name", "Icon", "Remove"};
    for (unsigned i = 0; i < 4; ++i) row(u8, 37 + i * 18, items[i], ctx->menu_sel == i);
    footer(ctx, u8, "OK:select", "BACK:browse");
}
static void type_handle(ui_ctx_t *ctx, const ui_event_t *event)
{
    if (event->type == UI_EV_UP && ctx->type_sel) --ctx->type_sel;
    if (event->type == UI_EV_DOWN && ctx->type_sel < 2) ++ctx->type_sel;
    if (event->type == UI_EV_BACK) { ui_nav_pop(ctx); return; }
    if (event->type == UI_EV_OK) {
        ctx->draft.transport = ctx->type_sel;
        ui_nav_push(ctx, UI_SCREEN_NAME, NULL);
    }
}
static void type_render(ui_ctx_t *ctx, u8g2_t *u8)
{
    title(u8, "Connect", "1/3");
    row(u8, 28, "IR", ctx->type_sel == 0);
    row(u8, 49, "BT", ctx->type_sel == 1);
    row(u8, 70, "WIFI", ctx->type_sel == 2);
    u8g2_SetFont(u8, u8g2_font_5x8_tf);
    center(u8, 103, ctx->type_sel ? "Coming soon" : "IR learning");
    footer(ctx, u8, "OK:next", "BACK:cancel");
}
static char keyboard_char(unsigned page, unsigned index)
{
    static const char numbers[] = "0123456789.-_!?@#:/()+*=&%,;";
    return page == 2 ? numbers[index] : (char)((page == 1 ? 'A' : 'a') + index);
}
static void name_next(ui_ctx_t *ctx)
{
    size_t length = strlen(ctx->draft.name);
    while (length && ctx->draft.name[length - 1] == ' ') ctx->draft.name[--length] = 0;
    size_t first = 0;
    while (ctx->draft.name[first] == ' ') ++first;
    if (first) memmove(ctx->draft.name, ctx->draft.name + first, length - first + 1);
    if (!ctx->draft.name[0]) { notice(ctx, "Enter a name"); return; }
    if (ctx->editing) {
        if (app_remote_update(ctx->active_remote, &ctx->draft) == ESP_OK) ui_nav_pop(ctx);
        else notice(ctx, "Save failed");
    } else ui_nav_push(ctx, UI_SCREEN_ICON, NULL);
}
static void name_handle(ui_ctx_t *ctx, const ui_event_t *event)
{
    if (event->type == UI_EV_BACK) { ui_nav_pop(ctx); return; }
    if (event->type == UI_EV_KEY && custom_key(event->value) && !event->repeat) {
        ctx->keyboard_page = (ctx->keyboard_page + 1) % 3;
        return;
    }
    unsigned *selected = &ctx->keyboard_sel;
    if (event->type == UI_EV_LEFT && *selected % 5) --*selected;
    if (event->type == UI_EV_RIGHT && *selected % 5 < 4) ++*selected;
    if (event->type == UI_EV_UP && *selected >= 5) *selected -= 5;
    if (event->type == UI_EV_DOWN && *selected < 25) *selected += 5;
    if (event->type != UI_EV_OK) return;
    size_t length = strlen(ctx->draft.name);
    if (*selected == 29) { name_next(ctx); return; }
    if (*selected == 28) { ctx->keyboard_page = (ctx->keyboard_page + 1) % 3; return; }
    if (*selected == 27) { if (length) ctx->draft.name[length - 1] = 0; return; }
    if (length >= REMOTE_NAME_MAX) { notice(ctx, "12 char max"); return; }
    ctx->draft.name[length] = *selected == 26 ? ' ' : keyboard_char(ctx->keyboard_page, *selected);
    ctx->draft.name[length + 1] = 0;
}
static void name_render(ui_ctx_t *ctx, u8g2_t *u8)
{
    char counter[24];
    snprintf(counter, sizeof(counter), "%u/12", (unsigned)strlen(ctx->draft.name));
    title(u8, "Name", counter);
    u8g2_DrawFrame(u8, 1, 19, 62, 24);
    u8g2_SetFont(u8, u8g2_font_6x10_tf);
    char first[10];
    snprintf(first, sizeof(first), "%.9s", ctx->draft.name);
    u8g2_DrawStr(u8, 4, 29, first);
    u8g2_DrawStr(u8, 4, 39, strlen(ctx->draft.name) > 9 ? ctx->draft.name + 9 : "_");
    for (unsigned i = 0; i < 30; ++i) {
        int x = 2 + (i % 5) * 12, y = 46 + (i / 5) * 11;
        bool selected = ctx->keyboard_sel == i;
        if (selected) { u8g2_DrawBox(u8, x, y, 11, 10); u8g2_SetDrawColor(u8, 0); }
        char label[3] = {0};
        if (i < 26) label[0] = keyboard_char(ctx->keyboard_page, i);
        else if (i == 26) label[0] = '_';
        else if (i == 27) label[0] = '<';
        else if (i == 28) label[0] = '^';
        else { label[0] = '-'; label[1] = '>'; }
        u8g2_SetFont(u8, i == 29 ? u8g2_font_5x8_tf : u8g2_font_6x10_tf);
        u8g2_DrawStr(u8, x + (11 - u8g2_GetStrWidth(u8, label)) / 2, y + 7, label);
        u8g2_SetDrawColor(u8, 1);
    }
    static const char *const pages[] = {"abc", "ABC", "123"};
    char hint[17];
    snprintf(hint, sizeof(hint), "CUST:%s", pages[ctx->keyboard_page]);
    footer(ctx, u8, hint, "OK:key ->:go");
}
static void icon_handle(ui_ctx_t *ctx, const ui_event_t *event)
{
    if (event->type == UI_EV_BACK) { ui_nav_pop(ctx); return; }
    if (event->type == UI_EV_LEFT && ctx->icon_sel) --ctx->icon_sel;
    if (event->type == UI_EV_RIGHT && ctx->icon_sel < REMOTE_ICON_COUNT - 1) ++ctx->icon_sel;
    if (event->type == UI_EV_UP && ctx->icon_sel >= 3) ctx->icon_sel -= 3;
    if (event->type == UI_EV_DOWN && ctx->icon_sel + 3 < REMOTE_ICON_COUNT) ctx->icon_sel += 3;
    if (event->type != UI_EV_OK) return;
    ctx->draft.icon = ctx->icon_sel;
    if (ctx->editing) {
        if (app_remote_update(ctx->active_remote, &ctx->draft) == ESP_OK) ui_nav_pop(ctx);
        else notice(ctx, "Save failed");
        return;
    }
    unsigned index;
    if (app_remote_create(&ctx->draft, &index) != ESP_OK) { notice(ctx, "Save failed"); return; }
    unsigned transport = ctx->draft.transport;
    ctx->home_sel = index;
    ui_nav_home(ctx);
    ctx->active_remote = index;
    ui_nav_push(ctx, transport == REMOTE_IR ? UI_SCREEN_IR_KEYS : UI_SCREEN_CONSTRUCTION, NULL);
}
static void icon_render(ui_ctx_t *ctx, u8g2_t *u8)
{
    char counter[24];
    snprintf(counter, sizeof(counter), "%u/24", ctx->icon_sel + 1);
    title(u8, "Icon", counter);
    unsigned page = ctx->icon_sel / 9;
    for (unsigned i = 0; i < 9; ++i) {
        unsigned index = page * 9 + i;
        if (index >= REMOTE_ICON_COUNT) break;
        int x = 2 + (i % 3) * 21, y = 20 + (i / 3) * 24;
        bool selected = index == ctx->icon_sel;
        if (selected) { u8g2_DrawBox(u8, x, y, 19, 21); u8g2_SetDrawColor(u8, 0); }
        ui_draw_icon(u8, x + 1, y + 2, index);
        u8g2_SetDrawColor(u8, 1);
    }
    u8g2_SetFont(u8, u8g2_font_6x10_tf);
    center(u8, 105, ui_icon_name(ctx->icon_sel));
    footer(ctx, u8, ctx->editing ? "OK:save icon" : "OK:create",
           ctx->editing ? "BACK:editor" : "BACK:name");
}
void ui_cancel_learning(ui_ctx_t *ctx)
{
    if (!ctx->ir.request) return;
    app_cmd_t command = {.type = APP_CMD_IR_LEARN_CANCEL, .request_id = ctx->ir.request};
    (void)app_send_cmd(&command, pdMS_TO_TICKS(50));
    ctx->ir.request = 0; // Late results can never attach to another key.
}
static void learn_start(ui_ctx_t *ctx, bool replace)
{
    bool same_screen = screen_id(ctx) == UI_SCREEN_IR_WAIT;
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
static void keys_handle(ui_ctx_t *ctx, const ui_event_t *event)
{
    if (event->src != UI_SRC_KEYPAD || event->repeat || event->type == UI_EV_OK_LONG
        || event->type == UI_EV_BACK_LONG || event->type == UI_EV_HOME_LONG) return;
    unsigned key = event->value;
    remote_t remote;
    if (!remote_key_assignable(key) || !app_remote_get(ctx->active_remote, &remote)) return;
    ctx->ir.key = key;
    ctx->ir.code = remote.keys[key - 1];
    if (remote.assigned & (1u << (key - 1))) ui_nav_push(ctx, UI_SCREEN_IR_REPLACE, NULL);
    else learn_start(ctx, false);
}
static void keys_render(ui_ctx_t *ctx, u8g2_t *u8)
{
    remote_t remote;
    if (!app_remote_get(ctx->active_remote, &remote)) return;
    title(u8, "Learn keys", "IR");
    u8g2_SetFont(u8, u8g2_font_6x10_tf);
    center(u8, 30, remote.name);
    ui_draw_icon(u8, 24, 37, remote.icon);
    center(u8, 68, "Press key");
    center(u8, 80, "to learn IR");
    char count[16];
    snprintf(count, sizeof(count), "%u/20 saved", remote_assigned_count(&remote));
    u8g2_SetFont(u8, u8g2_font_5x8_tf);
    center(u8, 99, count);
    footer(ctx, u8, "Hold BACK:back", "Hold HOME:exit");
}
static void code_render(u8g2_t *u8, ir_nec_code_t code, int y)
{
    char line[16];
    u8g2_SetFont(u8, u8g2_font_6x10_tf);
    center(u8, y, "Protocol NEC");
    snprintf(line, sizeof(line), "Addr %04X", code.addr);
    center(u8, y + 12, line);
    snprintf(line, sizeof(line), "Cmd  %04X", code.cmd);
    center(u8, y + 24, line);
}
static void replace_render(ui_ctx_t *ctx, u8g2_t *u8)
{
    title(u8, "Key saved", NULL);
    u8g2_SetFont(u8, u8g2_font_6x10_tf);
    center(u8, 30, remote_key_name(ctx->ir.key));
    code_render(u8, ctx->ir.code, 45);
    row(u8, 88, "Re-record", true);
    footer(ctx, u8, "OK:re-record", "Hold BACK");
}
static void wait_handle(ui_ctx_t *ctx, const ui_event_t *event)
{
    if (event->repeat) return;
    if (event->type == UI_EV_OK && (ctx->ir.status == UI_IR_FAILED || ctx->ir.status == UI_IR_TIMEOUT)) {
        learn_start(ctx, false);
    }
}
static void wait_render(ui_ctx_t *ctx, u8g2_t *u8)
{
    title(u8, "Learn IR", NULL);
    u8g2_SetFont(u8, u8g2_font_6x10_tf);
    center(u8, 30, remote_key_name(ctx->ir.key));
    bool retry = ctx->ir.status == UI_IR_TIMEOUT || ctx->ir.status == UI_IR_FAILED;
    if (retry) {
        center(u8, 50, ctx->ir.status == UI_IR_TIMEOUT ? "Timed out" : "Read error");
        center(u8, 65, "No IR saved");
        u8g2_SetFont(u8, u8g2_font_5x8_tf);
        center(u8, 96, "OK to retry");
    } else {
        center(u8, 49, ctx->ir.status == UI_IR_UNSUPPORTED ? "NEC only" : "Waiting");
        center(u8, 61, "for IR");
        center(u8, 73, "command");
        u8g2_SetFont(u8, u8g2_font_5x8_tf);
        center(u8, 92, "Press source");
        center(u8, 104, "remote key");
    }
    footer(ctx, u8, "Hold BACK", "to cancel");
}
static void result_handle(ui_ctx_t *ctx, const ui_event_t *event)
{
    if (event->type == UI_EV_OK) {
        if (ctx->ir.status == UI_IR_SAVE_FAILED) {
            if (app_remote_set_code(ctx->active_remote, ctx->ir.key, ctx->ir.code) == ESP_OK)
                ctx->ir.status = UI_IR_SAVED;
            else notice(ctx, "Save failed");
        } else ui_nav_pop(ctx);
    }
}
static void result_render(ui_ctx_t *ctx, u8g2_t *u8)
{
    title(u8, ctx->ir.status == UI_IR_SAVED ? "Saved" : "Save failed", NULL);
    u8g2_SetFont(u8, u8g2_font_6x10_tf);
    center(u8, 30, remote_key_name(ctx->ir.key));
    code_render(u8, ctx->ir.code, 48);
    row(u8, 90, ctx->ir.status == UI_IR_SAVED ? "Next key" : "Retry save", true);
    footer(ctx, u8, ctx->ir.status == UI_IR_SAVED ? "OK:next key" : "OK:retry", "Hold BACK");
}
static void remove_handle(ui_ctx_t *ctx, const ui_event_t *event)
{
    if (event->type == UI_EV_UP || event->type == UI_EV_DOWN) ctx->menu_sel ^= 1;
    if (event->type == UI_EV_BACK) { ui_nav_pop(ctx); ctx->menu_sel = 3; return; }
    if (event->type != UI_EV_OK) return;
    if (!ctx->menu_sel) { ui_nav_pop(ctx); ctx->menu_sel = 3; return; }
    if (app_remote_remove(ctx->active_remote) == ESP_OK) ui_nav_home(ctx);
    else notice(ctx, "Delete failed");
}
static void remove_render(ui_ctx_t *ctx, u8g2_t *u8)
{
    title(u8, "Remove?", NULL);
    remote_t remote;
    if (!app_remote_get(ctx->active_remote, &remote)) return;
    u8g2_SetFont(u8, u8g2_font_6x10_tf);
    center(u8, 34, remote.name);
    u8g2_SetFont(u8, u8g2_font_5x8_tf);
    center(u8, 48, "Erase remote");
    center(u8, 57, "and keys?");
    row(u8, 70, "Cancel", ctx->menu_sel == 0);
    row(u8, 90, "Remove", ctx->menu_sel == 1);
    footer(ctx, u8, "OK:confirm", "BACK:cancel");
}
static void construction_render(ui_ctx_t *ctx, u8g2_t *u8)
{
    remote_t remote;
    if (!app_remote_get(ctx->active_remote, &remote)) return;
    title(u8, "Setup", remote_transport_name(remote.transport));
    ui_draw_icon(u8, 24, 27, remote.icon);
    u8g2_SetFont(u8, u8g2_font_6x10_tf);
    center(u8, 61, "Under");
    center(u8, 73, "Construction");
    row(u8, 91, "Exit", true);
    footer(ctx, u8, "Remote saved", "OK:exit");
}
static void screen_enter(ui_ctx_t *ctx, void *param)
{
    (void)param;
    if (screen_id(ctx) == UI_SCREEN_SPLASH) ctx->splash_started = now_ms();
}
static void screen_exit(ui_ctx_t *ctx)
{
    if (screen_id(ctx) == UI_SCREEN_IR_WAIT) ui_cancel_learning(ctx);
}
static void screen_handle(ui_ctx_t *ctx, const ui_event_t *event)
{
    if (event->type == UI_EV_TICK) {
        if (ctx->notice[0] && (int32_t)(event->ts_ms - ctx->notice_until) >= 0) {
            ctx->notice[0] = 0;
            ctx->dirty = true;
        }
        if (screen_id(ctx) == UI_SCREEN_SPLASH && (uint32_t)(event->ts_ms - ctx->splash_started) >= 2000)
            ui_nav_replace(ctx, UI_SCREEN_HOME, NULL);
        return;
    }
    switch (screen_id(ctx)) {
        case UI_SCREEN_HOME: home_handle(ctx, event); break;
        case UI_SCREEN_REMOTE: remote_handle(ctx, event); break;
        case UI_SCREEN_EDIT: edit_handle(ctx, event); break;
        case UI_SCREEN_ADD_TYPE: type_handle(ctx, event); break;
        case UI_SCREEN_NAME: name_handle(ctx, event); break;
        case UI_SCREEN_ICON: icon_handle(ctx, event); break;
        case UI_SCREEN_IR_KEYS: keys_handle(ctx, event); break;
        case UI_SCREEN_IR_REPLACE:
            if (event->type == UI_EV_OK) learn_start(ctx, true);
            break;
        case UI_SCREEN_IR_WAIT: wait_handle(ctx, event); break;
        case UI_SCREEN_IR_RESULT: result_handle(ctx, event); break;
        case UI_SCREEN_REMOVE: remove_handle(ctx, event); break;
        case UI_SCREEN_CONSTRUCTION:
            if (event->type == UI_EV_OK || event->type == UI_EV_BACK) ui_nav_home(ctx);
            break;
        default: break;
    }
}
static void screen_render(ui_ctx_t *ctx, u8g2_t *u8)
{
    switch (screen_id(ctx)) {
        case UI_SCREEN_SPLASH:
            u8g2_SetFont(u8, u8g2_font_6x10_tf);
            center(u8, 59, "UNIVERSAL");
            center(u8, 74, "REMOTE");
            break;
        case UI_SCREEN_HOME: home_render(ctx, u8); break;
        case UI_SCREEN_REMOTE: remote_render(ctx, u8); break;
        case UI_SCREEN_EDIT: edit_render(ctx, u8); break;
        case UI_SCREEN_ADD_TYPE: type_render(ctx, u8); break;
        case UI_SCREEN_NAME: name_render(ctx, u8); break;
        case UI_SCREEN_ICON: icon_render(ctx, u8); break;
        case UI_SCREEN_IR_KEYS: keys_render(ctx, u8); break;
        case UI_SCREEN_IR_REPLACE: replace_render(ctx, u8); break;
        case UI_SCREEN_IR_WAIT: wait_render(ctx, u8); break;
        case UI_SCREEN_IR_RESULT: result_render(ctx, u8); break;
        case UI_SCREEN_REMOVE: remove_render(ctx, u8); break;
        case UI_SCREEN_CONSTRUCTION: construction_render(ctx, u8); break;
        default: break;
    }
}
void ui_handle_app_event(ui_ctx_t *ctx, const app_evt_t *event)
{
    if (event->request_id && event->request_id == ctx->ir.request && screen_id(ctx) == UI_SCREEN_IR_WAIT) {
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
