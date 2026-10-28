#include "ui_internal.h"

bool ui_open_bt(ui_ctx_t *ctx, const remote_t *remote)
{
    remote_t configured = *remote;
    // Pairing state never changes a profile's BLE identity. Move unpaired
    // profiles to isolated key storage; existing bonds keep their legacy store
    // until explicit Unpair/Pair new, so working pairings are preserved.
    if (!configured.bt.bonded && configured.bt.bond_store == BT_BOND_STORE_LEGACY) {
        configured.bt.bond_store = BT_BOND_STORE_PROFILE;
        if (app_remote_update(ctx->active_remote, &configured) != ESP_OK) {
            ui_notice(ctx, "Save failed"); return false;
        }
    }
    uint32_t session = ++ctx->sequence;
    if (!bt_runtime_open(&configured, ctx->active_remote, session)) { ui_notice(ctx, "BT busy"); return false; }
    ctx->bt_session = session;
    ctx->bt_status = (bt_snapshot_t){.session = session, .state = BT_STARTING};
    ctx->bt_power_pending = ctx->bt_remove_pending = false;
    return true;
}

void ui_bt_device_handle(ui_ctx_t *ctx, const ui_event_t *event)
{
    remote_t selected;
    if (!app_remote_get(ctx->active_remote, &selected)) return;
    bool mouse = selected.bt.kind == BT_REMOTE_MOUSE;
    if (event->type == UI_EV_MODE) { ui_enter_remote_mode(ctx); return; }
    if (event->type == UI_EV_BACK) { ui_nav_pop(ctx); return; }
    if (event->type == UI_EV_UP && ctx->bt_menu_sel) --ctx->bt_menu_sel;
    if (event->type == UI_EV_DOWN && ctx->bt_menu_sel < (mouse ? 5 : 4)) ++ctx->bt_menu_sel;
    if (event->type != UI_EV_OK) return;
    if (ctx->bt_menu_sel == 0) {
        if (ctx->bt_status.state == BT_CONNECTED || ctx->bt_status.state == BT_SUSPENDED) {
            ui_notice(ctx, "Already paired"); return;
        }
        remote_t remote;
        if (app_remote_get(ctx->active_remote, &remote)) (void)ui_open_bt(ctx, &remote);
    } else if (ctx->bt_menu_sel == 1 || ctx->bt_menu_sel == 2) {
        ctx->bt_confirm_action = ctx->bt_menu_sel;
        ctx->menu_sel = 0;
        ui_nav_push(ctx, UI_SCREEN_BT_CONFIRM, NULL);
    } else if (ctx->bt_menu_sel == 3) {
        ctx->bt_key_sel = 1;
        ui_nav_push(ctx, UI_SCREEN_BT_KEYS, NULL);
    } else if (mouse && ctx->bt_menu_sel == 4) {
        ctx->bt_speed_draft = bt_mouse_speed(&selected);
        ui_nav_push(ctx, UI_SCREEN_BT_SPEED, NULL);
    } else {
        ctx->menu_sel = 0;
        ui_nav_push(ctx, UI_SCREEN_EDIT, NULL);
    }
}

void ui_bt_device_render(ui_ctx_t *ctx, u8g2_t *u8)
{
    remote_t remote;
    if (!app_remote_get(ctx->active_remote, &remote)) return;
    bool mouse = remote.bt.kind == BT_REMOTE_MOUSE;
    ui_title(u8, mouse ? "Mouse" : "Keyboard", "BT");
    u8g2_SetFont(u8, u8g2_font_6x10_tf);
    bool progress = ctx->bt_status.state == BT_STARTING || ctx->bt_status.state == BT_STOPPING || ctx->bt_status.state == BT_ERROR;
    ui_center(u8, 29, progress && ctx->bt_status.step ? ctx->bt_status.step : remote.name);
    u8g2_SetFont(u8, u8g2_font_5x8_tf);
    char status_line[24];
    snprintf(status_line, sizeof(status_line), "BT error %04X", (unsigned)ctx->bt_status.error);
    ui_center(u8, 42, ctx->bt_status.state == BT_ERROR ? status_line : bt_runtime_status_name(ctx->bt_status.state));
    static const char *const items[] = {"Reconnect", "Pair new", "Unpair", "Key map", "Edit remote"};
    static const char *const mouse_items[] = {"Reconnect", "Pair new", "Unpair", "Key map", "Speed", "Edit remote"};
    char speed[20];
    snprintf(speed, sizeof(speed), "Speed %u/10", bt_mouse_speed(&remote));
    unsigned first = ctx->bt_menu_sel > 1 ? ctx->bt_menu_sel - 1 : 0;
    if (first > (mouse ? 3 : 2)) first = mouse ? 3 : 2;
    for (unsigned i = first; i < first + 3; ++i)
        ui_row(u8, 50 + (i - first) * 18, mouse ? (i == 4 ? speed : mouse_items[i]) : items[i], ctx->bt_menu_sel == i);
    ui_footer(ctx, u8, ctx->bt_status.state == BT_PAIRING && ctx->bt_status.error != ESP_OK
        ? "Forget on host" : ctx->bt_status.state == BT_UNPAIRED ? "Forget on host" : "MODE:use", "BACK:BT off");
}

void ui_bt_speed_handle(ui_ctx_t *ctx, const ui_event_t *event)
{
    if (event->type == UI_EV_BACK) { ui_nav_pop(ctx); return; }
    if ((event->type == UI_EV_UP || event->type == UI_EV_RIGHT || (event->type == UI_EV_KEY && event->value == REMOTE_KEY_VOL_UP))
        && ctx->bt_speed_draft < BT_MOUSE_SPEED_MAX) ++ctx->bt_speed_draft;
    if ((event->type == UI_EV_DOWN || event->type == UI_EV_LEFT || (event->type == UI_EV_KEY && event->value == REMOTE_KEY_VOL_DOWN))
        && ctx->bt_speed_draft > 1) --ctx->bt_speed_draft;
    if (event->type != UI_EV_OK) return;
    remote_t remote;
    if (app_remote_get(ctx->active_remote, &remote) && ui_save_mouse_speed(ctx, &remote, ctx->bt_speed_draft)) ui_nav_pop(ctx);
}

void ui_bt_speed_render(ui_ctx_t *ctx, u8g2_t *u8)
{
    ui_title(u8, "Speed", "BT");
    char speed[16];
    snprintf(speed, sizeof(speed), "%u / 10", ctx->bt_speed_draft);
    u8g2_SetFont(u8, u8g2_font_6x10_tf);
    ui_center(u8, 48, speed);
    u8g2_SetFont(u8, u8g2_font_5x8_tf);
    ui_center(u8, 70, "UP:faster"); ui_center(u8, 83, "DOWN:slower");
    ui_footer(ctx, u8, "OK:save", "BACK:cancel");
}

void ui_bt_confirm_handle(ui_ctx_t *ctx, const ui_event_t *event)
{
    if (event->type == UI_EV_UP || event->type == UI_EV_DOWN) ctx->menu_sel ^= 1;
    if (event->type == UI_EV_BACK || (event->type == UI_EV_OK && !ctx->menu_sel)) { ui_nav_pop(ctx); return; }
    if (event->type != UI_EV_OK) return;
    if (bt_runtime_forget(ctx->bt_session, ctx->bt_confirm_action == 1)) {
        ui_nav_pop(ctx);
        ui_notice(ctx, "Updating BT");
    } else ui_notice(ctx, "BT busy");
}

void ui_bt_confirm_render(ui_ctx_t *ctx, u8g2_t *u8)
{
    ui_title(u8, ctx->bt_confirm_action == 1 ? "Pair new?" : "Unpair?", "BT");
    u8g2_SetFont(u8, u8g2_font_5x8_tf);
    ui_center(u8, 33, "Forget saved");
    ui_center(u8, 44, "connection?");
    ui_center(u8, 59, "Also forget");
    ui_center(u8, 70, "on host");
    ui_row(u8, 77, "Cancel", ctx->menu_sel == 0);
    ui_row(u8, 94, "Confirm", ctx->menu_sel == 1);
    ui_footer(ctx, u8, "OK:select", "BACK:cancel");
}

void ui_bt_keys_handle(ui_ctx_t *ctx, const ui_event_t *event)
{
    remote_t remote;
    if (!app_remote_get(ctx->active_remote, &remote)) return;
    if (remote.bt.kind == BT_REMOTE_KEYBOARD && remote.bt.preset >= BT_PRESET_TEXT) {
        if (event->type == UI_EV_MODE) { ui_nav_pop(ctx); return; }
        if (event->src != UI_SRC_KEYPAD || event->repeat || !remote_key_assignable(event->value)
            || event->type == UI_EV_BACK_LONG || event->type == UI_EV_HOME_LONG
            || event->type == UI_EV_OK_LONG) return;
        ctx->bt_key_sel = event->value;
        if (remote.bt.preset == BT_PRESET_TEXT) {
            app_remote_get_bt_text(ctx->active_remote, ctx->bt_key_sel, ctx->bt_text_draft);
            ctx->keyboard_sel = ctx->keyboard_page = 0;
            ui_nav_push(ctx, UI_SCREEN_BT_TEXT, NULL);
        } else {
            bt_binding_t binding = bt_profile_binding(&remote, ctx->bt_key_sel);
            ctx->bt_binding_sel = 0;
            for (unsigned i = 0; i < bt_keycode_count(); ++i)
                if (bt_keycode_at(i) == binding.keycode) ctx->bt_binding_sel = i;
            ctx->bt_modifiers = binding.modifiers;
            ui_nav_push(ctx, UI_SCREEN_BT_BINDING, NULL);
        }
        return;
    }
    if (event->type == UI_EV_BACK) { ui_nav_pop(ctx); return; }
    if (event->type == UI_EV_UP && ctx->bt_key_sel > 1) {
        --ctx->bt_key_sel;
        if (ctx->bt_key_sel == REMOTE_KEY_MODE) --ctx->bt_key_sel;
    }
    if (event->type == UI_EV_DOWN && ctx->bt_key_sel < REMOTE_KEY_COUNT) {
        ++ctx->bt_key_sel;
        if (ctx->bt_key_sel == REMOTE_KEY_MODE) ++ctx->bt_key_sel;
    }
}

void ui_bt_keys_render(ui_ctx_t *ctx, u8g2_t *u8)
{
    remote_t remote;
    if (!app_remote_get(ctx->active_remote, &remote)) return;
    ui_title(u8, "Key map", "BT");
    u8g2_SetFont(u8, u8g2_font_5x8_tf);
    bool mouse = remote.bt.kind == BT_REMOTE_MOUSE;
    ui_center(u8, 31, mouse ? bt_mouse_mode_name(remote.bt.mouse_mode) : bt_preset_name(remote.bt.preset));
    if (!mouse && remote.bt.preset >= BT_PRESET_TEXT) {
        u8g2_SetFont(u8, u8g2_font_6x10_tf);
        ui_center(u8, 51, "Press a key");
        ui_center(u8, 64, "to assign");
        u8g2_SetFont(u8, u8g2_font_5x8_tf);
        char count[24];
        snprintf(count, sizeof(count), "%u/20 assigned", remote_assigned_count(&remote));
        ui_center(u8, 83, count);
        ui_center(u8, 99, "MODE:done");
        ui_footer(ctx, u8, "Hold BACK:", "BT menu");
        return;
    }
    u8g2_SetFont(u8, u8g2_font_6x10_tf);
    ui_center(u8, 53, remote_key_name(ctx->bt_key_sel));
    bt_binding_t binding = bt_profile_binding(&remote, ctx->bt_key_sel);
    ui_center(u8, 72, mouse ? bt_mouse_key_label(remote.bt.mouse_mode, ctx->bt_key_sel)
        : binding.keycode ? bt_keycode_label(binding.keycode) : "Unassigned");
    u8g2_SetFont(u8, u8g2_font_5x8_tf);
    if (mouse && remote.bt.mouse_mode == BT_MOUSE_PATTERNS) {
        ui_center(u8, 91, "Tap:toggle"); ui_center(u8, 102, "Hold:until up");
    } else if (mouse && ctx->bt_key_sel == REMOTE_KEY_OK) {
        ui_center(u8, 91, "Hold:drag"); ui_center(u8, 102, "Tap:drop");
    } else if (!mouse && remote.bt.preset == BT_PRESET_FIRE_TV && ctx->bt_key_sel == REMOTE_KEY_POWER) {
        ui_center(u8, 91, "Hold POWER:"); ui_center(u8, 102, "Ctrl+Alt+Del");
    } else ui_center(u8, 99, "Mapping fixed");
    ui_footer(ctx, u8, "UP/DOWN:keys", "BACK:BT menu");
}

void ui_bt_binding_handle(ui_ctx_t *ctx, const ui_event_t *event)
{
    if (event->type == UI_EV_BACK) { ui_nav_pop(ctx); return; }
    if (event->type == UI_EV_KEY && !event->repeat) {
        if (event->value == REMOTE_KEY_CUSTOM4) ctx->bt_modifiers ^= 1;
        if (event->value == REMOTE_KEY_CUSTOM5) ctx->bt_modifiers ^= 2;
        if (event->value == REMOTE_KEY_CUSTOM6) ctx->bt_modifiers ^= 4;
    }
    unsigned *selected = &ctx->bt_binding_sel, count = bt_keycode_count();
    if (event->type == UI_EV_LEFT && *selected % 2) --*selected;
    if (event->type == UI_EV_RIGHT && !(*selected % 2) && *selected + 1 < count) ++*selected;
    if (event->type == UI_EV_UP && *selected >= 2) *selected -= 2;
    if (event->type == UI_EV_DOWN && *selected + 2 < count) *selected += 2;
    if (event->type != UI_EV_OK) return;
    bt_binding_t binding = {.keycode = bt_keycode_at(*selected), .modifiers = *selected ? ctx->bt_modifiers : 0};
    if (app_remote_set_bt_binding(ctx->active_remote, ctx->bt_key_sel, binding) == ESP_OK) {
        ui_nav_pop(ctx); ui_notice(ctx, binding.keycode ? "Key saved" : "Key cleared");
    } else ui_notice(ctx, "Save failed");
}

void ui_bt_binding_render(ui_ctx_t *ctx, u8g2_t *u8)
{
    ui_title(u8, remote_key_name(ctx->bt_key_sel), NULL);
    char modifiers[32];
    snprintf(modifiers, sizeof(modifiers), "%s%s%s", ctx->bt_modifiers & 1 ? "Ctrl+" : "",
        ctx->bt_modifiers & 2 ? "Shift+" : "", ctx->bt_modifiers & 4 ? "Alt+" : "");
    u8g2_SetFont(u8, u8g2_font_5x8_tf);
    ui_center(u8, 25, modifiers[0] ? modifiers : "No modifiers");
    ui_center(u8, 36, bt_keycode_label(bt_keycode_at(ctx->bt_binding_sel)));
    unsigned first = (ctx->bt_binding_sel / 10) * 10;
    unsigned last_page = bt_keycode_count() > 10 ? ((bt_keycode_count() - 10 + 1) / 2) * 2 : 0;
    if (first > last_page) first = last_page;
    for (unsigned i = first; i < first + 10 && i < bt_keycode_count(); ++i) {
        int x = (i % 2) * 32, y = 42 + ((i - first) / 2) * 13;
        if (i == ctx->bt_binding_sel) { u8g2_DrawBox(u8, x, y, 31, 12); u8g2_SetDrawColor(u8, 0); }
        u8g2_SetFont(u8, u8g2_font_4x6_tf);
        u8g2_DrawStr(u8, x + 2, y + 8, bt_keycode_label(bt_keycode_at(i)));
        u8g2_SetDrawColor(u8, 1);
    }
    ui_footer(ctx, u8, "4:Ctrl 5:Shift", "6:Alt OK:save");
}
