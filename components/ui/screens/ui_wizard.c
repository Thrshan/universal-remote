#include "ui_internal.h"

static void create_remote(ui_ctx_t *ctx);

static bool custom_key(unsigned key)
{
    return key == REMOTE_KEY_CUSTOM2 || key == REMOTE_KEY_CUSTOM3
        || key == REMOTE_KEY_CUSTOM4 || key == REMOTE_KEY_CUSTOM5 || key == REMOTE_KEY_CUSTOM6;
}

void ui_type_handle(ui_ctx_t *ctx, const ui_event_t *event)
{
    if (event->type == UI_EV_UP && ctx->type_sel) --ctx->type_sel;
    if (event->type == UI_EV_DOWN && ctx->type_sel < 2) ++ctx->type_sel;
    if (event->type == UI_EV_BACK) { ui_nav_pop(ctx); return; }
    if (event->type == UI_EV_OK) {
        if (ctx->draft.transport != ctx->type_sel) {
            memset(ctx->draft.keys, 0, sizeof(ctx->draft.keys));
            ctx->draft.assigned = 0;
        }
        ctx->draft.transport = ctx->type_sel;
        ctx->bt_type_sel = 0;
        ui_nav_push(ctx, ctx->type_sel == REMOTE_BT ? UI_SCREEN_BT_TYPE : UI_SCREEN_NAME, NULL);
    }
}

void ui_type_render(ui_ctx_t *ctx, u8g2_t *u8)
{
    ui_title(u8, "Connect", "1/3");
    ui_row(u8, 28, "IR", ctx->type_sel == 0);
    ui_row(u8, 49, "BT", ctx->type_sel == 1);
    ui_row(u8, 70, "WIFI", ctx->type_sel == 2);
    u8g2_SetFont(u8, u8g2_font_5x8_tf);
    ui_center(u8, 103, ctx->type_sel == REMOTE_BT ? "Keyboard/mouse" : ctx->type_sel ? "Coming soon" : "IR learning");
    ui_footer(ctx, u8, "OK:next", "BACK:cancel");
}

void ui_bt_type_handle(ui_ctx_t *ctx, const ui_event_t *event)
{
    if (event->type == UI_EV_UP && ctx->bt_type_sel) --ctx->bt_type_sel;
    if (event->type == UI_EV_DOWN && ctx->bt_type_sel < 2) ++ctx->bt_type_sel;
    if (event->type == UI_EV_BACK) { ui_nav_pop(ctx); return; }
    if (event->type != UI_EV_OK) return;
    if (ctx->bt_type_sel == BT_REMOTE_CONTROLLER) { ui_notice(ctx, "On hold"); return; }
    memset(&ctx->draft.bt, 0, sizeof(ctx->draft.bt));
    ctx->draft.bt.kind = ctx->bt_type_sel;
    ctx->draft.bt.mouse_speed = BT_MOUSE_SPEED_DEFAULT;
    remote_bt_ensure_identity(&ctx->draft);
    ctx->draft.bt.bond_store = BT_BOND_STORE_PROFILE;
    ctx->draft.assigned = ctx->bt_type_sel == BT_REMOTE_MOUSE ? bt_mouse_mask(BT_MOUSE_MAPPED) : bt_keymap_mask();
    ui_nav_push(ctx, UI_SCREEN_NAME, NULL);
}

void ui_bt_type_render(ui_ctx_t *ctx, u8g2_t *u8)
{
    ui_title(u8, "BT device", NULL);
    ui_row(u8, 28, "Keyboard", ctx->bt_type_sel == 0);
    ui_row(u8, 49, "Mouse", ctx->bt_type_sel == 1);
    ui_row(u8, 70, "Controller", ctx->bt_type_sel == 2);
    u8g2_SetFont(u8, u8g2_font_5x8_tf);
    ui_center(u8, 102, ctx->bt_type_sel == BT_REMOTE_CONTROLLER ? "On hold" : "Choose preset");
    ui_footer(ctx, u8, "OK:select", "BACK:connect");
}

static char keyboard_char(unsigned page, unsigned index)
{
    static const char numbers[] = "0123456789.-_!?@#:/()+*=&%,;";
    static const char symbols[] = "[]{}\\|\"'$%^<>~`-+=:;!?@#&*";
    if (page == 3) return symbols[index % (sizeof(symbols) - 1)];
    return page == 2 ? numbers[index] : (char)((page == 1 ? 'A' : 'a') + index);
}

static void name_next(ui_ctx_t *ctx)
{
    size_t length = strlen(ctx->draft.name);
    while (length && ctx->draft.name[length - 1] == ' ') ctx->draft.name[--length] = 0;
    size_t first = 0;
    while (ctx->draft.name[first] == ' ') ++first;
    if (first) memmove(ctx->draft.name, ctx->draft.name + first, length - first + 1);
    if (!ctx->draft.name[0]) { ui_notice(ctx, "Enter a name"); return; }
    if (ctx->editing) {
        remote_t current;
        if (ctx->draft.transport == REMOTE_BT && app_remote_get(ctx->active_remote, &current))
            ctx->draft.bt = current.bt; // Pairing can finish while the name editor is open.
        if (app_remote_update(ctx->active_remote, &ctx->draft) == ESP_OK) ui_nav_pop(ctx);
        else ui_notice(ctx, "Save failed");
    } else ui_nav_push(ctx, UI_SCREEN_ICON, NULL);
}

void ui_name_handle(ui_ctx_t *ctx, const ui_event_t *event)
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
    if (length >= REMOTE_NAME_MAX) { ui_notice(ctx, "12 char max"); return; }
    ctx->draft.name[length] = *selected == 26 ? ' ' : keyboard_char(ctx->keyboard_page, *selected);
    ctx->draft.name[length + 1] = 0;
}

void ui_name_render(ui_ctx_t *ctx, u8g2_t *u8)
{
    bool text = ui_screen_id(ctx) == UI_SCREEN_BT_TEXT;
    const char *value = text ? ctx->bt_text_draft : ctx->draft.name;
    size_t length = strlen(value);
    char counter[24];
    snprintf(counter, sizeof(counter), "%u/%u", (unsigned)length, text ? BT_TEXT_MAX : REMOTE_NAME_MAX);
    ui_title(u8, text ? "Text" : "Name", counter);
    u8g2_DrawFrame(u8, 1, 19, 62, 24);
    u8g2_SetFont(u8, u8g2_font_6x10_tf);
    char first[10];
    // Keep the end of a long string visible while entering it.
    if (text && length > 18) value += length - 18;
    snprintf(first, sizeof(first), "%.9s", value);
    u8g2_DrawStr(u8, 4, 29, first);
    u8g2_DrawStr(u8, 4, 39, strlen(value) > 9 ? value + 9 : "_");
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
    static const char *const pages[] = {"abc", "ABC", "123", "#+="};
    char hint[17];
    snprintf(hint, sizeof(hint), "CUST:%s", pages[ctx->keyboard_page]);
    ui_footer(ctx, u8, hint, text ? "OK:key ->:save" : "OK:key ->:go");
}

void ui_bt_text_handle(ui_ctx_t *ctx, const ui_event_t *event)
{
    if (event->type == UI_EV_BACK) { ui_nav_pop(ctx); return; }
    if (event->type == UI_EV_KEY && custom_key(event->value) && !event->repeat) {
        ctx->keyboard_page = (ctx->keyboard_page + 1) % 4; return;
    }
    unsigned *selected = &ctx->keyboard_sel;
    if (event->type == UI_EV_LEFT && *selected % 5) --*selected;
    if (event->type == UI_EV_RIGHT && *selected % 5 < 4) ++*selected;
    if (event->type == UI_EV_UP && *selected >= 5) *selected -= 5;
    if (event->type == UI_EV_DOWN && *selected < 25) *selected += 5;
    if (event->type != UI_EV_OK) return;
    size_t length = strlen(ctx->bt_text_draft);
    if (*selected == 29) {
        if (app_remote_set_bt_text(ctx->active_remote, ctx->bt_key_sel, ctx->bt_text_draft) == ESP_OK) {
            ui_nav_pop(ctx); ui_notice(ctx, length ? "Text saved" : "Key cleared");
        } else ui_notice(ctx, "Save failed");
        return;
    }
    if (*selected == 28) { ctx->keyboard_page = (ctx->keyboard_page + 1) % 4; return; }
    if (*selected == 27) { if (length) ctx->bt_text_draft[length - 1] = 0; return; }
    if (length >= BT_TEXT_MAX) { ui_notice(ctx, "64 char max"); return; }
    ctx->bt_text_draft[length] = *selected == 26 ? ' ' : keyboard_char(ctx->keyboard_page, *selected);
    ctx->bt_text_draft[length + 1] = 0;
}

static void create_remote(ui_ctx_t *ctx)
{
    unsigned index;
    if (app_remote_create(&ctx->draft, &index) != ESP_OK) { ui_notice(ctx, "Save failed"); return; }
    unsigned transport = ctx->draft.transport;
    ctx->home_sel = index;
    ui_nav_home(ctx);
    ctx->active_remote = index;
    if (transport == REMOTE_BT) {
        remote_t remote;
        if (app_remote_get(index, &remote) && ui_open_bt(ctx, &remote)) {
            ctx->bt_menu_sel = 0;
            ui_nav_push(ctx, UI_SCREEN_BT_DEVICE, NULL);
            if (remote.bt.kind == BT_REMOTE_KEYBOARD && remote.bt.preset >= BT_PRESET_TEXT) {
                ctx->bt_key_sel = 1;
                ui_nav_push(ctx, UI_SCREEN_BT_KEYS, NULL);
            }
        }
    } else ui_nav_push(ctx, transport == REMOTE_IR ? UI_SCREEN_IR_KEYS : UI_SCREEN_CONSTRUCTION, NULL);
}

void ui_icon_handle(ui_ctx_t *ctx, const ui_event_t *event)
{
    if (event->type == UI_EV_BACK) { ui_nav_pop(ctx); return; }
    if (event->type == UI_EV_LEFT && ctx->icon_sel) --ctx->icon_sel;
    if (event->type == UI_EV_RIGHT && ctx->icon_sel < REMOTE_ICON_COUNT - 1) ++ctx->icon_sel;
    if (event->type == UI_EV_UP && ctx->icon_sel >= 3) ctx->icon_sel -= 3;
    if (event->type == UI_EV_DOWN && ctx->icon_sel + 3 < REMOTE_ICON_COUNT) ctx->icon_sel += 3;
    if (event->type != UI_EV_OK) return;
    ctx->draft.icon = ctx->icon_sel;
    if (ctx->editing) {
        remote_t current;
        if (ctx->draft.transport == REMOTE_BT && app_remote_get(ctx->active_remote, &current))
            ctx->draft.bt = current.bt;
        if (app_remote_update(ctx->active_remote, &ctx->draft) == ESP_OK) ui_nav_pop(ctx);
        else ui_notice(ctx, "Save failed");
        return;
    }
    if (ctx->draft.transport == REMOTE_BT) {
        ctx->bt_preset_sel = ctx->draft.bt.kind == BT_REMOTE_MOUSE ? ctx->draft.bt.mouse_mode : ctx->draft.bt.preset;
        ui_nav_push(ctx, UI_SCREEN_BT_PRESET, NULL);
    } else create_remote(ctx);
}

void ui_icon_render(ui_ctx_t *ctx, u8g2_t *u8)
{
    char counter[24];
    snprintf(counter, sizeof(counter), "%u/24", ctx->icon_sel + 1);
    ui_title(u8, "Icon", counter);
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
    ui_center(u8, 105, ui_icon_name(ctx->icon_sel));
    ui_footer(ctx, u8, ctx->editing ? "OK:save icon" : ctx->draft.transport == REMOTE_BT ? "OK:preset" : "OK:create",
           ctx->editing ? "BACK:editor" : "BACK:name");
}

void ui_bt_preset_handle(ui_ctx_t *ctx, const ui_event_t *event)
{
    bool mouse = ctx->draft.bt.kind == BT_REMOTE_MOUSE;
    if (event->type == UI_EV_BACK) { ui_nav_pop(ctx); return; }
    if (event->type == UI_EV_UP && ctx->bt_preset_sel) --ctx->bt_preset_sel;
    if (event->type == UI_EV_DOWN && ctx->bt_preset_sel < (mouse ? BT_MOUSE_PATTERNS : BT_PRESET_CUSTOM)) ++ctx->bt_preset_sel;
    if (event->type != UI_EV_OK) return;
    if (mouse) ctx->draft.bt.mouse_mode = ctx->bt_preset_sel;
    else ctx->draft.bt.preset = ctx->bt_preset_sel;
    memset(ctx->draft.bt.bindings, 0, sizeof(ctx->draft.bt.bindings));
    ctx->draft.assigned = 0;
    ctx->draft.assigned = mouse ? bt_mouse_mask(ctx->draft.bt.mouse_mode) : bt_profile_mask(&ctx->draft);
    create_remote(ctx);
}

void ui_bt_preset_render(ui_ctx_t *ctx, u8g2_t *u8)
{
    bool mouse = ctx->draft.bt.kind == BT_REMOTE_MOUSE;
    ui_title(u8, mouse ? "Mouse mode" : "BT preset", NULL);
    for (unsigned i = 0; i <= (mouse ? BT_MOUSE_PATTERNS : BT_PRESET_CUSTOM); ++i)
        ui_row(u8, 24 + i * 21, mouse ? bt_mouse_mode_name(i) : bt_preset_name(i), ctx->bt_preset_sel == i);
    ui_footer(ctx, u8, "OK:select", "BACK:icon");
}
