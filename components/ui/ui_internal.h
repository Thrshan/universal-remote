#pragma once
// Internal drawing helpers and feature callbacks; not part of the UI component API.
#include "ui_nav.h"
#include "ui_icons.h"
#include "u8g2.h"
#include <stdio.h>
#include <string.h>

uint32_t ui_now_ms(void);
ui_screen_id_t ui_screen_id(ui_ctx_t *ctx);
void ui_notice(ui_ctx_t *ctx, const char *message);
void ui_center(u8g2_t *u8, int y, const char *text);
void ui_title(u8g2_t *u8, const char *text, const char *right);
void ui_footer(ui_ctx_t *ctx, u8g2_t *u8, const char *first, const char *second);
void ui_row(u8g2_t *u8, int y, const char *text, bool selected);
void ui_plus_icon(u8g2_t *u8, int x, int y);
void ui_home_render(ui_ctx_t *ctx, u8g2_t *u8);
bool ui_open_bt(ui_ctx_t *ctx, const remote_t *remote);
void ui_enter_remote_mode(ui_ctx_t *ctx);
void ui_home_handle(ui_ctx_t *ctx, const ui_event_t *event);
void ui_settings_handle(ui_ctx_t *ctx, const ui_event_t *event);
void ui_settings_render(ui_ctx_t *ctx, u8g2_t *u8);
void ui_about_render(ui_ctx_t *ctx, u8g2_t *u8);
void ui_storage_handle(ui_ctx_t *ctx, const ui_event_t *event);
void ui_storage_render(ui_ctx_t *ctx, u8g2_t *u8);
bool ui_save_mouse_speed(ui_ctx_t *ctx, remote_t *remote, unsigned speed);
void ui_remote_handle(ui_ctx_t *ctx, const ui_event_t *event);
void ui_remote_render(ui_ctx_t *ctx, u8g2_t *u8);
void ui_edit_handle(ui_ctx_t *ctx, const ui_event_t *event);
void ui_edit_render(ui_ctx_t *ctx, u8g2_t *u8);
void ui_type_handle(ui_ctx_t *ctx, const ui_event_t *event);
void ui_type_render(ui_ctx_t *ctx, u8g2_t *u8);
void ui_bt_type_handle(ui_ctx_t *ctx, const ui_event_t *event);
void ui_bt_type_render(ui_ctx_t *ctx, u8g2_t *u8);
void ui_bt_device_handle(ui_ctx_t *ctx, const ui_event_t *event);
void ui_bt_device_render(ui_ctx_t *ctx, u8g2_t *u8);
void ui_bt_speed_handle(ui_ctx_t *ctx, const ui_event_t *event);
void ui_bt_speed_render(ui_ctx_t *ctx, u8g2_t *u8);
void ui_bt_confirm_handle(ui_ctx_t *ctx, const ui_event_t *event);
void ui_bt_confirm_render(ui_ctx_t *ctx, u8g2_t *u8);
void ui_bt_keys_handle(ui_ctx_t *ctx, const ui_event_t *event);
void ui_bt_keys_render(ui_ctx_t *ctx, u8g2_t *u8);
void ui_bt_binding_handle(ui_ctx_t *ctx, const ui_event_t *event);
void ui_bt_binding_render(ui_ctx_t *ctx, u8g2_t *u8);
void ui_name_handle(ui_ctx_t *ctx, const ui_event_t *event);
void ui_name_render(ui_ctx_t *ctx, u8g2_t *u8);
void ui_bt_text_handle(ui_ctx_t *ctx, const ui_event_t *event);
void ui_icon_handle(ui_ctx_t *ctx, const ui_event_t *event);
void ui_icon_render(ui_ctx_t *ctx, u8g2_t *u8);
void ui_bt_preset_handle(ui_ctx_t *ctx, const ui_event_t *event);
void ui_bt_preset_render(ui_ctx_t *ctx, u8g2_t *u8);
void ui_cancel_learning(ui_ctx_t *ctx);
void ui_learn_start(ui_ctx_t *ctx, bool replace);
void ui_keys_handle(ui_ctx_t *ctx, const ui_event_t *event);
void ui_keys_render(ui_ctx_t *ctx, u8g2_t *u8);
void ui_replace_render(ui_ctx_t *ctx, u8g2_t *u8);
void ui_wait_handle(ui_ctx_t *ctx, const ui_event_t *event);
void ui_wait_render(ui_ctx_t *ctx, u8g2_t *u8);
void ui_result_handle(ui_ctx_t *ctx, const ui_event_t *event);
void ui_result_render(ui_ctx_t *ctx, u8g2_t *u8);
void ui_remove_handle(ui_ctx_t *ctx, const ui_event_t *event);
void ui_remove_render(ui_ctx_t *ctx, u8g2_t *u8);
void ui_construction_render(ui_ctx_t *ctx, u8g2_t *u8);
void ui_handle_app_event(ui_ctx_t *ctx, const app_evt_t *event);
void ui_register_screens(ui_ctx_t *ctx);
