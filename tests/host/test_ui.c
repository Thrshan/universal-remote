#include "ui_nav.h"
#include "u8g2.h"
#include "oled_port.h"
#include "host_support.h"
#include "bt_advertising.h"
#include "bt_output.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static u8g2_t display;
static unsigned checks;
#define CHECK(condition) do { ++checks; if (!(condition)) { fprintf(stderr, "Failed line %d: %s\n", __LINE__, #condition); return 1; } } while (0)
static ui_screen_id_t current(ui_ctx_t *ctx) { return ui_nav_current(ctx)->id; }
static void key(ui_ctx_t *ctx, unsigned number)
{
    ui_event_type_t type = UI_EV_KEY;
    switch (number) {
        case 2: type = UI_EV_MODE; break;
        case 5: type = UI_EV_UP; break;
        case 6: type = UI_EV_LEFT; break;
        case 7: type = UI_EV_OK; break;
        case 8: type = UI_EV_RIGHT; break;
        case 9: type = UI_EV_DOWN; break;
        case 10: type = UI_EV_BACK; break;
        case 12: type = UI_EV_HOME; break;
    }
    ui_event_t event = {.type = type, .src = UI_SRC_KEYPAD, .value = number, .ts_ms = host_time};
    ui_nav_dispatch(ctx, &event);
}
static void hold_home(ui_ctx_t *ctx)
{
    ui_event_t event = {.type = UI_EV_HOME_LONG, .src = UI_SRC_KEYPAD, .value = 12};
    ui_nav_dispatch(ctx, &event);
}
static void hold_back(ui_ctx_t *ctx)
{
    // The scanner first emits the ordinary press, then a long event at 700 ms.
    key(ctx, REMOTE_KEY_BACK);
    ui_event_t event = {.type = UI_EV_BACK_LONG, .src = UI_SRC_KEYPAD, .value = REMOTE_KEY_BACK};
    ui_nav_dispatch(ctx, &event);
}
static void release(ui_ctx_t *ctx, unsigned number)
{
    ui_event_t event = {.type = UI_EV_KEY_RELEASE, .src = UI_SRC_KEYPAD, .value = number};
    ui_nav_dispatch(ctx, &event);
}
static void bt_peer(ui_ctx_t *ctx, bool bonded)
{
    app_evt_t event = {.type = APP_EVT_BT_PEER, .request_id = ctx->bt_session};
    event.bt.bonded = bonded;
    if (bonded) { event.bt.peer[0] = 0xA1; event.bt.peer[5] = 0xB2; }
    ui_handle_app_event(ctx, &event);
}
static void tick(ui_ctx_t *ctx, unsigned time)
{
    host_time = time;
    ui_event_t event = {.type = UI_EV_TICK, .src = UI_SRC_SYSTEM, .ts_ms = time};
    ui_nav_dispatch(ctx, &event);
}
static void drain_commands(void)
{
    app_cmd_t command;
    while (xQueueReceive(app_get_ctx()->cmd_q, &command, 0) == pdTRUE) {}
}
static void capture(ui_ctx_t *ctx, ir_nec_code_t code)
{
    app_evt_t event = {.type = APP_EVT_IR_LEARNED, .request_id = ctx->ir.request};
    event.ir.code = code;
    ui_handle_app_event(ctx, &event);
}
static void snapshot(ui_ctx_t *ctx, const char *name)
{
    ctx->dirty = true;
    ui_nav_render_if_dirty(ctx);
    char path[256];
    snprintf(path, sizeof(path), "build_host/%s.pbm", name);
    FILE *file = fopen(path, "w");
    assert(file);
    fprintf(file, "P1\n64 128\n");
    const uint8_t *buffer = u8g2_GetBufferPtr(&display);
    // Invert the U8G2_R1 framebuffer transformation for logical portrait pixels.
    for (unsigned y = 0; y < 128; ++y) {
        for (unsigned x = 0; x < 64; ++x) {
            unsigned physical_x = 127 - y, physical_y = x;
            unsigned lit = (buffer[(physical_y / 8) * 128 + physical_x] >> (physical_y % 8)) & 1;
            fprintf(file, "%u ", !lit);
        }
        fputc('\n', file);
    }
    fclose(file);
}
static uint8_t dummy(u8x8_t *u8, uint8_t message, uint8_t argument, void *pointer)
{ (void)u8; (void)message; (void)argument; (void)pointer; return 1; }

static void add_bt(ui_ctx_t *ctx, const char *name, unsigned preset)
{
    ui_nav_home(ctx);
    ctx->home_sel = app_remote_count();
    key(ctx, REMOTE_KEY_OK); key(ctx, REMOTE_KEY_DOWN); key(ctx, REMOTE_KEY_OK); key(ctx, REMOTE_KEY_OK);
    snprintf(ctx->draft.name, sizeof(ctx->draft.name), "%s", name);
    ctx->keyboard_sel = 29;
    key(ctx, REMOTE_KEY_OK); key(ctx, REMOTE_KEY_OK);
    ctx->bt_preset_sel = preset;
    key(ctx, REMOTE_KEY_OK);
}

typedef struct { unsigned sends, fail_on, cancel_on; bool active; bt_key_report_t reports[8]; } output_probe_t;
static bool probe_active(void *context) { return ((output_probe_t *)context)->active; }
static bool probe_send(void *context, const bt_key_report_t *report)
{
    output_probe_t *probe = context;
    if (probe->sends < 8) probe->reports[probe->sends] = *report;
    return ++probe->sends != probe->fail_on;
}
static void probe_pause(void *context, unsigned milliseconds)
{
    output_probe_t *probe = context;
    if (probe->sends == probe->cancel_on) probe->active = false;
}

static int bluetooth_preset_checks(ui_ctx_t *ctx)
{
    remote_t remote;
    bt_key_report_t report;
    for (unsigned i = 0; i < bt_keycode_count(); ++i) {
        bt_binding_t binding = {.keycode = bt_keycode_at(i)};
        CHECK(bt_binding_valid(binding));
        CHECK(bt_binding_report(binding, &report) == (i != 0));
    }
    for (unsigned ch = 32; ch <= 126; ++ch) CHECK(bt_text_report(ch, &report));
    CHECK(bt_text_report('\t', &report) && report.data[2] == 0x2B);
    CHECK(bt_text_report('\n', &report) && report.data[2] == 0x28);
    CHECK(!bt_text_report((char)0xC3, &report));
    CHECK(!bt_text_report(0, &report));
    CHECK(!bt_binding_valid((bt_binding_t){.keycode = BT_KEY_MUTE, .modifiers = 1}));
    output_probe_t probe = {.active = true};
    bt_output_t output = {.context = &probe, .active = probe_active, .send = probe_send, .pause = probe_pause};
    CHECK(bt_output_text(&output, "aA!"));
    CHECK(probe.sends == 6 && probe.reports[0].data[2] == 4 && probe.reports[2].data[0] == 2
        && probe.reports[4].data[2] == 0x1E);
    for (unsigned i = 1; i < probe.sends; i += 2)
        for (unsigned j = 0; j < 8; ++j) CHECK(probe.reports[i].data[j] == 0);
    for (unsigned fail = 1; fail <= 2; ++fail) {
        probe = (output_probe_t){.active = true, .fail_on = fail};
        CHECK(!bt_output_text(&output, "abc") && probe.sends == 2 && probe.reports[1].data[2] == 0);
    }
    probe = (output_probe_t){.active = true, .cancel_on = 1};
    CHECK(!bt_output_text(&output, "abc") && probe.sends == 2 && probe.reports[1].data[2] == 0);
    probe = (output_probe_t){.active = false};
    CHECK(!bt_output_text(&output, "a") && probe.sends == 0);
    probe.active = true;
    CHECK(!bt_output_text(&output, "a\x01") && probe.sends == 0);

    add_bt(ctx, "Media", BT_PRESET_MEDIA);
    unsigned media_index = ctx->active_remote;
    CHECK(current(ctx) == UI_SCREEN_BT_DEVICE);
    CHECK(app_remote_get(media_index, &remote) && remote.bt.preset == BT_PRESET_MEDIA && remote_assigned_count(&remote) == 17);
    CHECK(bt_profile_report(&remote, REMOTE_KEY_VOL_UP, false, &report) && report.report_id == 2 && report.data[0] == 0xE9);
    CHECK(bt_profile_report(&remote, REMOTE_KEY_VOL_DOWN, false, &report) && report.data[0] == 0xEA);
    CHECK(bt_profile_report(&remote, REMOTE_KEY_PLAY_PAUSE, false, &report) && report.data[0] == 0xCD);
    CHECK(bt_profile_report(&remote, REMOTE_KEY_VOL_CLICK, false, &report) && report.data[0] == 0xE2);
    CHECK(!bt_profile_report(&remote, REMOTE_KEY_POWER, true, &report));
    ctx->bt_menu_sel = 3; key(ctx, REMOTE_KEY_OK);
    ctx->bt_key_sel = REMOTE_KEY_VOL_UP; snapshot(ctx, "25-bt-media");
    key(ctx, REMOTE_KEY_BACK);
    host_bt_status.state = BT_CONNECTED;
    key(ctx, REMOTE_KEY_MODE); key(ctx, REMOTE_KEY_PLAY_PAUSE);
    CHECK(host_bt_report.report_id == 2 && host_bt_report.data[0] == 0xCD && host_bt_report_count == 2);
    unsigned taps = host_bt_taps;
    key(ctx, REMOTE_KEY_POWER); release(ctx, REMOTE_KEY_POWER);
    CHECK(host_bt_taps == taps + 1 && host_bt_report.data[0] == 0x30);

    add_bt(ctx, "Custom", BT_PRESET_CUSTOM);
    unsigned custom_index = ctx->active_remote;
    CHECK(current(ctx) == UI_SCREEN_BT_KEYS && app_remote_get(custom_index, &remote) && remote_assigned_count(&remote) == 0);
    snapshot(ctx, "26-bt-custom-picker");
    key(ctx, REMOTE_KEY_POWER);
    CHECK(current(ctx) == UI_SCREEN_BT_BINDING && ctx->bt_key_sel == REMOTE_KEY_POWER);
    key(ctx, REMOTE_KEY_RIGHT);
    CHECK(ctx->bt_binding_sel == 1); // Clear/A is the first row of two columns.
    key(ctx, REMOTE_KEY_DOWN); CHECK(ctx->bt_binding_sel == 3);
    key(ctx, REMOTE_KEY_LEFT); CHECK(ctx->bt_binding_sel == 2);
    key(ctx, REMOTE_KEY_UP); CHECK(ctx->bt_binding_sel == 0);
    key(ctx, REMOTE_KEY_RIGHT);
    key(ctx, REMOTE_KEY_CUSTOM4); key(ctx, REMOTE_KEY_CUSTOM5); key(ctx, REMOTE_KEY_CUSTOM6);
    CHECK(ctx->bt_modifiers == 7);
    key(ctx, REMOTE_KEY_CUSTOM4); CHECK(ctx->bt_modifiers == 6);
    key(ctx, REMOTE_KEY_CUSTOM4); CHECK(ctx->bt_modifiers == 7);
    snapshot(ctx, "27-bt-custom-modifiers");
    host_fail_save = 2; key(ctx, REMOTE_KEY_OK);
    CHECK(current(ctx) == UI_SCREEN_BT_BINDING && app_remote_get(custom_index, &remote) && remote_assigned_count(&remote) == 0);
    host_fail_save = 0; key(ctx, REMOTE_KEY_OK);
    CHECK(current(ctx) == UI_SCREEN_BT_KEYS && app_remote_get(custom_index, &remote) && remote_assigned_count(&remote) == 1);
    CHECK(bt_profile_report(&remote, REMOTE_KEY_POWER, false, &report) && report.data[0] == 7 && report.data[2] == 4);
    key(ctx, REMOTE_KEY_UP);
    CHECK(current(ctx) == UI_SCREEN_BT_BINDING && ctx->bt_modifiers == 0);
    ctx->bt_binding_sel = bt_keycode_count() - 1;
    ctx->notice[0] = 0;
    snapshot(ctx, "28-bt-standard-keys");
    key(ctx, REMOTE_KEY_OK);
    CHECK(app_remote_get(custom_index, &remote) && bt_profile_report(&remote, REMOTE_KEY_UP, false, &report) && report.data[0] == 0x80);
    key(ctx, REMOTE_KEY_CUSTOM4); // This physical button is also independently assignable.
    CHECK(current(ctx) == UI_SCREEN_BT_BINDING && ctx->bt_key_sel == REMOTE_KEY_CUSTOM4);
    key(ctx, REMOTE_KEY_BACK); CHECK(current(ctx) == UI_SCREEN_BT_KEYS);
    hold_back(ctx); CHECK(current(ctx) == UI_SCREEN_BT_DEVICE); // A held BACK must exit its own key assignment.
    host_bt_status.state = BT_CONNECTED;
    key(ctx, REMOTE_KEY_MODE);
    taps = host_bt_taps;
    key(ctx, REMOTE_KEY_POWER); release(ctx, REMOTE_KEY_POWER);
    CHECK(host_bt_taps == taps + 1 && host_bt_report.data[0] == 7 && host_bt_report.data[2] == 4);
    key(ctx, REMOTE_KEY_MODE);
    ctx->bt_menu_sel = 3; key(ctx, REMOTE_KEY_OK); key(ctx, REMOTE_KEY_POWER);
    CHECK(ctx->bt_binding_sel == 1 && ctx->bt_modifiers == 7);
    ctx->bt_binding_sel = 0; key(ctx, REMOTE_KEY_OK);
    CHECK(app_remote_get(custom_index, &remote) && !bt_profile_key_assigned(&remote, REMOTE_KEY_POWER));

    add_bt(ctx, "Text", BT_PRESET_TEXT);
    unsigned text_index = ctx->active_remote;
    CHECK(current(ctx) == UI_SCREEN_BT_KEYS);
    snapshot(ctx, "29-bt-text-picker");
    key(ctx, REMOTE_KEY_HOME);
    CHECK(current(ctx) == UI_SCREEN_BT_TEXT && ctx->bt_key_sel == REMOTE_KEY_HOME);
    key(ctx, REMOTE_KEY_OK); // a
    key(ctx, REMOTE_KEY_CUSTOM2); ctx->keyboard_sel = 1; key(ctx, REMOTE_KEY_OK); // B
    ctx->keyboard_sel = 26; key(ctx, REMOTE_KEY_OK);
    key(ctx, REMOTE_KEY_CUSTOM2); ctx->keyboard_sel = 2; key(ctx, REMOTE_KEY_OK); // 2
    CHECK(strcmp(ctx->bt_text_draft, "aB 2") == 0);
    key(ctx, REMOTE_KEY_CUSTOM2); CHECK(ctx->keyboard_page == 3);
    ctx->keyboard_sel = 0; key(ctx, REMOTE_KEY_OK); // [
    ctx->keyboard_sel = 27; key(ctx, REMOTE_KEY_OK);
    CHECK(strcmp(ctx->bt_text_draft, "aB 2") == 0);
    snapshot(ctx, "30-bt-text-editor");
    ctx->keyboard_sel = 29; host_fail_save = 1; key(ctx, REMOTE_KEY_OK);
    char text[BT_TEXT_MAX + 1];
    CHECK(current(ctx) == UI_SCREEN_BT_TEXT && app_remote_get_bt_text(text_index, REMOTE_KEY_HOME, text) && !text[0]);
    host_fail_save = 0; key(ctx, REMOTE_KEY_OK);
    CHECK(current(ctx) == UI_SCREEN_BT_KEYS && app_remote_get_bt_text(text_index, REMOTE_KEY_HOME, text) && strcmp(text, "aB 2") == 0);
    key(ctx, REMOTE_KEY_UP);
    memset(ctx->bt_text_draft, 'x', BT_TEXT_MAX); ctx->bt_text_draft[BT_TEXT_MAX] = 0;
    ctx->keyboard_sel = 0; key(ctx, REMOTE_KEY_OK); CHECK(strlen(ctx->bt_text_draft) == BT_TEXT_MAX);
    snapshot(ctx, "31-bt-text-limit");
    ctx->keyboard_sel = 29; key(ctx, REMOTE_KEY_OK);
    CHECK(!app_remote_get_bt_text(text_index, REMOTE_KEY_MODE, text));
    CHECK(app_remote_set_bt_text(text_index, REMOTE_KEY_MODE, "blocked") == ESP_ERR_INVALID_ARG);
    CHECK(app_remote_set_bt_text(text_index, REMOTE_KEY_HOME, "bad\x01") == ESP_ERR_INVALID_ARG);
    key(ctx, REMOTE_KEY_HOME);
    strcpy(ctx->bt_text_draft, "cancel"); key(ctx, REMOTE_KEY_BACK);
    CHECK(app_remote_get_bt_text(text_index, REMOTE_KEY_HOME, text) && strcmp(text, "aB 2") == 0);
    key(ctx, REMOTE_KEY_MODE); CHECK(current(ctx) == UI_SCREEN_BT_DEVICE);
    host_bt_status.state = BT_CONNECTED;
    key(ctx, REMOTE_KEY_MODE); key(ctx, REMOTE_KEY_HOME);
    CHECK(host_bt_report_count == 8 && host_bt_reports[0].data[2] == 4 && host_bt_reports[2].data[0] == 2
        && host_bt_reports[2].data[2] == 5 && host_bt_reports[4].data[2] == 0x2C && host_bt_reports[6].data[2] == 0x1F);
    for (unsigned i = 1; i < host_bt_report_count; i += 2)
        for (unsigned j = 0; j < 8; ++j) CHECK(host_bt_reports[i].data[j] == 0);
    taps = host_bt_taps;
    ui_event_t repeat = {.type = UI_EV_UP, .src = UI_SRC_KEYPAD, .value = REMOTE_KEY_UP, .repeat = true};
    ui_nav_dispatch(ctx, &repeat); CHECK(host_bt_taps == taps);
    key(ctx, REMOTE_KEY_UP); CHECK(host_bt_report_count == BT_TEXT_MAX * 2);
    key(ctx, REMOTE_KEY_MODE);
    ui_nav_home(ctx); app_state_init();
    CHECK(app_remote_get(custom_index, &remote) && remote.bt.preset == BT_PRESET_CUSTOM && remote_assigned_count(&remote) == 1);
    CHECK(app_remote_get(text_index, &remote) && remote.bt.preset == BT_PRESET_TEXT && remote_assigned_count(&remote) == 2);
    CHECK(app_remote_get_bt_text(text_index, REMOTE_KEY_HOME, text) && strcmp(text, "aB 2") == 0);
    CHECK(app_remote_set_bt_text(text_index, REMOTE_KEY_UP, "") == ESP_OK);
    CHECK(app_remote_get(text_index, &remote) && remote_assigned_count(&remote) == 1);
    host_fail_save = 2;
    CHECK(app_remote_remove(media_index) != ESP_OK);
    CHECK(app_remote_get_bt_text(text_index, REMOTE_KEY_HOME, text) && strcmp(text, "aB 2") == 0);
    host_fail_save = 0;
    CHECK(app_remote_remove(media_index) == ESP_OK);
    CHECK(app_remote_get_bt_text(text_index - 1, REMOTE_KEY_HOME, text) && strcmp(text, "aB 2") == 0);
    app_state_init();
    CHECK(app_remote_get_bt_text(text_index - 1, REMOTE_KEY_HOME, text) && strcmp(text, "aB 2") == 0);

    host_clear_storage();
    remote = (remote_t){.name = "Old Fire TV", .transport = REMOTE_BT, .assigned = bt_keymap_mask()};
    remote.bt.bonded = 1; remote.bt.peer[0] = 0xAA;
    host_v1_storage(&remote); app_state_init();
    CHECK(app_remote_get(0, &remote) && remote.bt.preset == BT_PRESET_FIRE_TV && remote.bt.bonded && remote.bt.peer[0] == 0xAA);
    CHECK(remote_assigned_count(&remote) == 12 && bt_profile_report(&remote, REMOTE_KEY_POWER, true, &report));
    CHECK(app_remote_update(0, &remote) == ESP_OK);
    app_state_init();
    CHECK(app_remote_get(0, &remote) && remote.bt.preset == BT_PRESET_FIRE_TV && remote.bt.bonded);
    // Version 2 never defined this spare byte; ignore its old contents without losing the bond.
    remote.bt.bond_store = 0xA5;
    host_version_storage(&remote, 2); app_state_init();
    CHECK(app_remote_get(0, &remote) && remote.bt.bond_store == BT_BOND_STORE_LEGACY && remote.bt.bonded);
    remote.bt.bond_store = BT_BOND_STORE_PROFILE;
    CHECK(app_remote_update(0, &remote) == ESP_OK);
    app_state_init();
    CHECK(app_remote_get(0, &remote) && remote.bt.bond_store == BT_BOND_STORE_PROFILE && remote.bt.bonded);
    return 0;
}

int main(void)
{
    bt_keyboard_init();
    CHECK(bt_configure_advertising(0x03C1) == ESP_OK);
    CHECK(host_adv_calls == 2 && host_adv_service == 0x1812 && host_adv_appearance == 0x03C1);
    CHECK(!host_adv_has_name && host_scan_has_name);
    host_adv_calls = 0; host_adv_fail_on = 1;
    CHECK(bt_configure_advertising(0x03C1) == ESP_FAIL && host_adv_calls == 1);
    host_adv_calls = 0; host_adv_fail_on = 2;
    CHECK(bt_configure_advertising(0x03C1) == ESP_FAIL && host_adv_calls == 2);
    host_adv_calls = host_adv_fail_on = 0;
    remote_t identity = {.transport = REMOTE_BT};
    CHECK(!remote_bt_has_identity(&identity));
    remote_bt_ensure_identity(&identity);
    CHECK(remote_bt_has_identity(&identity) && (identity.bt.local_addr[0] & 0xC0) == 0xC0);
    uint8_t previous_identity[6];
    memcpy(previous_identity, identity.bt.local_addr, 6);
    remote_bt_ensure_identity(&identity);
    CHECK(remote_bt_has_identity(&identity) && memcmp(previous_identity, identity.bt.local_addr, 6) == 0);
    memset(identity.bt.local_addr, 0xFF, 6);
    CHECK(!remote_bt_has_identity(&identity));
    // The transport metadata overlay must preserve all existing v1 recordings.
    CHECK(sizeof(remote_t) == 104);
    CHECK(sizeof(((remote_t *)0)->keys) == 84);
    bt_key_report_t report;
    const unsigned mapped[] = {REMOTE_KEY_BACK, REMOTE_KEY_CUSTOM4, REMOTE_KEY_CH_UP,
        REMOTE_KEY_PLAY_PAUSE, REMOTE_KEY_CH_DOWN, REMOTE_KEY_UP, REMOTE_KEY_DOWN,
        REMOTE_KEY_LEFT, REMOTE_KEY_RIGHT, REMOTE_KEY_OK, REMOTE_KEY_POWER};
    const uint8_t usages[] = {0x29,0x65,0x40,0x41,0x42,0x52,0x51,0x50,0x4F,0x28,0x66};
    for (unsigned i = 0; i < sizeof(mapped) / sizeof(mapped[0]); ++i) {
        CHECK(bt_keymap_report(mapped[i], false, &report));
        CHECK(report.report_id == 1 && report.length == 8 && report.data[0] == 0 && report.data[2] == usages[i]);
    }
    CHECK(bt_keymap_report(REMOTE_KEY_HOME, false, &report));
    CHECK(report.report_id == 2 && report.length == 2 && report.data[0] == 0x23 && report.data[1] == 0x02);
    CHECK(bt_keymap_report(REMOTE_KEY_POWER, true, &report) && report.data[0] == 5 && report.data[2] == 0x4C);
    CHECK(!bt_keymap_report(REMOTE_KEY_MODE, false, &report));
    CHECK(!bt_keymap_report(REMOTE_KEY_VOL_UP, false, &report));
    host_clear_storage();
    app_state_init();
    CHECK(app_remote_count() == 0);
    remote_t remote = {.name = "Living TV", .transport = REMOTE_IR, .icon = 0};
    unsigned index;
    host_fail_save = 1;
    CHECK(app_remote_create(&remote, &index) != ESP_OK);
    CHECK(app_remote_count() == 0);
    host_fail_save = 0;
    CHECK(app_remote_create(&remote, &index) == ESP_OK && index == 0);
    CHECK(app_remote_set_code(0, 2, (ir_nec_code_t){1, 2}) != ESP_OK);
    CHECK(app_remote_set_code(0, 1, (ir_nec_code_t){0x1234, 0x5678}) == ESP_OK);
    host_fail_save = 1;
    CHECK(app_remote_set_code(0, 1, (ir_nec_code_t){9, 9}) != ESP_OK);
    CHECK(app_remote_get(0, &remote) && remote.keys[0].cmd == 0x5678);
    CHECK(app_remote_remove(0) != ESP_OK && app_remote_count() == 1);
    host_fail_save = 2;
    CHECK(app_remote_set_code(0, 1, (ir_nec_code_t){9, 9}) != ESP_OK);
    CHECK(app_remote_get(0, &remote) && remote.keys[0].cmd == 0x5678);
    host_fail_save = 0;
    app_state_init();
    CHECK(app_remote_get(0, &remote) && remote.keys[0].addr == 0x1234);
    host_corrupt_storage();
    app_state_init();
    CHECK(app_get_ctx()->storage_error != ESP_OK);
    CHECK(app_remote_create(&remote, &index) != ESP_OK);
    host_clear_storage();
    host_legacy_storage(32);
    app_state_init();
    CHECK(app_remote_count() == 2);
    unsigned old = 0;
    for (unsigned r = 0; r < 2; ++r) {
        CHECK(app_remote_get(r, &remote));
        for (unsigned k = 1; k <= 21 && old < 32; ++k) {
            if (!remote_key_assignable(k)) continue;
            CHECK(remote.keys[k - 1].addr == old++);
        }
    }
    app_state_init();
    CHECK(app_remote_count() == 2); // Migration is not repeated after reboot.

    host_clear_storage();
    app_state_init();
    host_time = 0;
    oled_setup_portrait(&display, dummy, dummy);
    CHECK(u8g2_GetDisplayWidth(&display) == 64 && u8g2_GetDisplayHeight(&display) == 128);
    CHECK(u8g2_GetU8x8(&display)->display_cb == u8x8_d_sh1106_128x64_noname);
    CHECK(u8g2_GetU8x8(&display)->x_offset == 2);
    CHECK(u8x8_GetI2CAddress(u8g2_GetU8x8(&display)) == (0x3C << 1));
    ui_ctx_t ctx;
    ui_nav_init(&ctx, &display);
    ui_nav_push(&ctx, UI_SCREEN_SPLASH, NULL);
    snapshot(&ctx, "01-startup");
    key(&ctx, 7);
    tick(&ctx, 1999);
    CHECK(current(&ctx) == UI_SCREEN_SPLASH);
    tick(&ctx, 2000);
    CHECK(current(&ctx) == UI_SCREEN_HOME);
    snapshot(&ctx, "02-empty");
    key(&ctx, 2);
    CHECK(current(&ctx) == UI_SCREEN_HOME);
    key(&ctx, 7);
    CHECK(current(&ctx) == UI_SCREEN_ADD_TYPE);
    snapshot(&ctx, "03-connection");
    key(&ctx, 7);
    CHECK(current(&ctx) == UI_SCREEN_NAME);
    key(&ctx, 2);
    CHECK(current(&ctx) == UI_SCREEN_NAME); // MODE never accepts wizard steps.
    key(&ctx, 8); key(&ctx, 7);
    CHECK(strcmp(ctx.draft.name, "b") == 0);
    key(&ctx, 3);
    CHECK(ctx.keyboard_page == 1);
    key(&ctx, 7);
    CHECK(strcmp(ctx.draft.name, "bB") == 0);
    key(&ctx, 19);
    CHECK(ctx.keyboard_page == 2);
    key(&ctx, 7);
    CHECK(strcmp(ctx.draft.name, "bB1") == 0);
    ctx.keyboard_sel = 27;
    key(&ctx, 7);
    CHECK(strcmp(ctx.draft.name, "bB") == 0);
    strcpy(ctx.draft.name, "ABCDEFGHIJKL");
    ctx.keyboard_sel = 0;
    key(&ctx, 7);
    CHECK(strlen(ctx.draft.name) == 12);
    snapshot(&ctx, "keyboard-limit");
    ctx.notice[0] = 0;
    ctx.keyboard_page = 0;
    ctx.keyboard_sel = 29;
    strcpy(ctx.draft.name, "Living TV");
    snapshot(&ctx, "04-keyboard");
    key(&ctx, 12);
    CHECK(app_remote_count() == 0 && current(&ctx) == UI_SCREEN_HOME);
    key(&ctx, 7); key(&ctx, 7);
    strcpy(ctx.draft.name, "   ");
    ctx.keyboard_sel = 29;
    key(&ctx, 7);
    CHECK(current(&ctx) == UI_SCREEN_NAME);
    strcpy(ctx.draft.name, "Living TV");
    key(&ctx, 7);
    CHECK(current(&ctx) == UI_SCREEN_ICON && app_remote_count() == 0);
    key(&ctx, 10);
    CHECK(current(&ctx) == UI_SCREEN_NAME && app_remote_count() == 0);
    key(&ctx, 7);
    for (unsigned i = 0; i < REMOTE_ICON_COUNT; ++i) {
        ctx.icon_sel = i;
        snapshot(&ctx, i == 0 ? "05-icons" : i == 9 ? "06-icons" : i == 18 ? "07-icons" : "icon-last");
    }
    ctx.icon_sel = 0;
    host_fail_save = 1;
    key(&ctx, 7);
    CHECK(current(&ctx) == UI_SCREEN_ICON && app_remote_count() == 0);
    host_fail_save = 0;
    key(&ctx, 7);
    CHECK(current(&ctx) == UI_SCREEN_IR_KEYS && app_remote_count() == 1);
    snapshot(&ctx, "08-learn-keys");

    // BACK selects its binding; only a hold navigates away during learning.
    key(&ctx, 10);
    CHECK(current(&ctx) == UI_SCREEN_IR_WAIT && ctx.ir.key == 10);
    uint32_t canceled = ctx.ir.request;
    key(&ctx, 10);
    CHECK(current(&ctx) == UI_SCREEN_IR_WAIT && ctx.ir.request == canceled);
    hold_back(&ctx);
    CHECK(current(&ctx) == UI_SCREEN_IR_KEYS && ctx.ir.request == 0);
    app_evt_t late = {.type = APP_EVT_IR_LEARNED, .request_id = canceled, .ir.code = {99, 99}};
    ui_handle_app_event(&ctx, &late);
    CHECK(app_remote_get(0, &remote) && remote.assigned == 0);
    key(&ctx, 12);
    CHECK(current(&ctx) == UI_SCREEN_IR_WAIT && ctx.ir.key == 12);
    hold_home(&ctx);
    CHECK(current(&ctx) == UI_SCREEN_HOME && app_remote_count() == 1);
    key(&ctx, 7); key(&ctx, 7);
    CHECK(current(&ctx) == UI_SCREEN_IR_KEYS);

    // A hold that starts on the picker returns to its original parent even
    // though the initial BACK press opens a temporary recording screen.
    hold_back(&ctx);
    CHECK(current(&ctx) == UI_SCREEN_EDIT && ctx.ir.request == 0);
    key(&ctx, 7);
    CHECK(current(&ctx) == UI_SCREEN_IR_KEYS);

    // All 20 appliance keys are independently learnable, including OK/BACK/HOME.
    for (unsigned k = 1; k <= 21; ++k) {
        if (!remote_key_assignable(k)) continue;
        drain_commands();
        key(&ctx, k);
        CHECK(current(&ctx) == UI_SCREEN_IR_WAIT && ctx.ir.key == k);
        app_cmd_t command;
        CHECK(xQueueReceive(app_get_ctx()->cmd_q, &command, 0) == pdTRUE);
        CHECK(command.type == APP_CMD_IR_LEARN_ONESHOT && command.request_id == ctx.ir.request);
        if (k == 1) {
            snapshot(&ctx, "09-waiting");
            app_evt_t timeout = {.type = APP_EVT_IR_LEARN_TIMEOUT, .request_id = ctx.ir.request};
            ui_handle_app_event(&ctx, &timeout);
            int depth = ctx.top;
            key(&ctx, 7);
            CHECK(current(&ctx) == UI_SCREEN_IR_WAIT && ctx.top == depth && ctx.ir.request != 0);
            app_evt_t noise = {.type = APP_EVT_IR_UNSUPPORTED, .request_id = ctx.ir.request};
            ui_handle_app_event(&ctx, &noise);
            CHECK(ctx.ir.status == UI_IR_UNSUPPORTED);
        }
        if (k == 3) host_fail_save = 1;
        capture(&ctx, (ir_nec_code_t){0xAB00 + k, 0xCD00 + k});
        CHECK(current(&ctx) == UI_SCREEN_IR_RESULT);
        if (k == 3) {
            CHECK(ctx.ir.status == UI_IR_SAVE_FAILED);
            host_fail_save = 0;
            key(&ctx, 7);
            CHECK(ctx.ir.status == UI_IR_SAVED);
        }
        if (k == 1) snapshot(&ctx, "10-saved");
        key(&ctx, 7);
        CHECK(current(&ctx) == UI_SCREEN_IR_KEYS);
    }
    CHECK(app_remote_get(0, &remote) && remote_assigned_count(&remote) == 20);
    key(&ctx, 1);
    CHECK(current(&ctx) == UI_SCREEN_IR_REPLACE);
    snapshot(&ctx, "11-rerecord");
    key(&ctx, 2);
    CHECK(current(&ctx) == UI_SCREEN_IR_REPLACE);
    hold_back(&ctx);
    CHECK(current(&ctx) == UI_SCREEN_IR_KEYS);
    key(&ctx, 1); key(&ctx, 7);
    CHECK(current(&ctx) == UI_SCREEN_IR_WAIT);
    hold_back(&ctx);
    CHECK(current(&ctx) == UI_SCREEN_IR_KEYS);
    CHECK(app_remote_get(0, &remote) && remote.keys[0].cmd == 0xCD01);
    hold_home(&ctx);

    key(&ctx, 2);
    CHECK(current(&ctx) == UI_SCREEN_REMOTE);
    // Every mapped key transmits the selected remote's matching binding.
    for (unsigned k = 1; k <= 21; ++k) {
        if (!remote_key_assignable(k)) continue;
        drain_commands();
        key(&ctx, k);
        CHECK(current(&ctx) == UI_SCREEN_REMOTE);
        app_cmd_t command;
        CHECK(xQueueReceive(app_get_ctx()->cmd_q, &command, 0) == pdTRUE);
        CHECK(command.type == APP_CMD_IR_TX_SEND && command.tx.code.cmd == 0xCD00 + k);
        app_evt_t sent = {.type = APP_EVT_IR_TX_DONE, .request_id = command.request_id};
        ui_handle_app_event(&ctx, &sent);
    }
    snapshot(&ctx, "12-remote");
    drain_commands();
    hold_back(&ctx);
    CHECK(current(&ctx) == UI_SCREEN_REMOTE);
    app_cmd_t back_command;
    CHECK(xQueueReceive(app_get_ctx()->cmd_q, &back_command, 0) == pdTRUE);
    CHECK(back_command.type == APP_CMD_IR_TX_SEND && back_command.tx.code.cmd == 0xCD0A);
    CHECK(xQueueReceive(app_get_ctx()->cmd_q, &back_command, 0) == pdFALSE);
    key(&ctx, 2);
    CHECK(current(&ctx) == UI_SCREEN_HOME);
    snapshot(&ctx, "13-home");
    key(&ctx, 7);
    snapshot(&ctx, "14-editor");
    key(&ctx, 2);
    CHECK(current(&ctx) == UI_SCREEN_REMOTE); // Open-first flow, with the IR shortcut retained.
    key(&ctx, 2);
    CHECK(current(&ctx) == UI_SCREEN_EDIT);

    // Name/icon editing preserves every existing key binding.
    key(&ctx, 9); key(&ctx, 7);
    CHECK(current(&ctx) == UI_SCREEN_NAME);
    strcpy(ctx.draft.name, "Cancel name");
    key(&ctx, 10);
    CHECK(app_remote_get(0, &remote) && strcmp(remote.name, "Living TV") == 0);
    key(&ctx, 7);
    strcpy(ctx.draft.name, "Bedroom TV");
    ctx.keyboard_sel = 29;
    key(&ctx, 7);
    CHECK(current(&ctx) == UI_SCREEN_EDIT);
    CHECK(app_remote_get(0, &remote) && strcmp(remote.name, "Bedroom TV") == 0);
    key(&ctx, 9); key(&ctx, 7);
    CHECK(current(&ctx) == UI_SCREEN_ICON);
    ctx.icon_sel = 23;
    key(&ctx, 7);
    CHECK(current(&ctx) == UI_SCREEN_EDIT);
    CHECK(app_remote_get(0, &remote) && remote.icon == 23 && remote_assigned_count(&remote) == 20);

    // Bluetooth adds a role step; deferred roles cannot create dummy devices.
    key(&ctx, 12);
    for (unsigned transport = 1; transport <= 2; ++transport) {
        ctx.home_sel = app_remote_count();
        key(&ctx, 7);
        for (unsigned i = 0; i < transport; ++i) key(&ctx, 9);
        key(&ctx, 7);
        if (transport == REMOTE_BT) {
            CHECK(current(&ctx) == UI_SCREEN_BT_TYPE);
            snapshot(&ctx, "19-bt-type");
            key(&ctx, 9); key(&ctx, 7);
            CHECK(current(&ctx) == UI_SCREEN_BT_TYPE && app_remote_count() == 1);
            key(&ctx, 9); key(&ctx, 7);
            CHECK(current(&ctx) == UI_SCREEN_BT_TYPE && app_remote_count() == 1);
            key(&ctx, 5); key(&ctx, 5); key(&ctx, 7);
        }
        strcpy(ctx.draft.name, transport == 1 ? "Fire TV" : "WiFi Light");
        ctx.keyboard_sel = 29;
        key(&ctx, 7);
        ctx.icon_sel = transport == 1 ? 2 : 5;
        key(&ctx, 7);
        if (transport == REMOTE_BT) {
            CHECK(current(&ctx) == UI_SCREEN_BT_PRESET && app_remote_count() == 1 && host_bt_opens == 0);
            ctx.notice[0] = 0;
            snapshot(&ctx, "24-bt-presets");
            key(&ctx, REMOTE_KEY_BACK);
            CHECK(current(&ctx) == UI_SCREEN_ICON && ctx.icon_sel == 2);
            key(&ctx, REMOTE_KEY_OK);
            host_fail_save = 1;
            key(&ctx, REMOTE_KEY_OK);
            CHECK(current(&ctx) == UI_SCREEN_BT_PRESET && app_remote_count() == 1 && host_bt_opens == 0);
            host_fail_save = 0;
            key(&ctx, REMOTE_KEY_OK);
        }
        CHECK(current(&ctx) == (transport == REMOTE_BT ? UI_SCREEN_BT_DEVICE : UI_SCREEN_CONSTRUCTION));
        snapshot(&ctx, transport == 1 ? "15-bluetooth" : "16-wifi");
        if (transport == REMOTE_BT) {
            CHECK(host_bt_opens == 1);
            tick(&ctx, host_time + 1);
            CHECK(ctx.bt_status.state == BT_PAIRING);
            CHECK(app_remote_get(1, &remote) && remote_assigned_count(&remote) == 12);
            CHECK(remote_bt_has_identity(&remote));
            uint8_t paired_identity[6];
            memcpy(paired_identity, remote.bt.local_addr, 6);
            key(&ctx, 2);
            CHECK(current(&ctx) == UI_SCREEN_REMOTE);
            key(&ctx, REMOTE_KEY_UP);
            CHECK(host_bt_taps == 0 && strcmp(ctx.tx_status, "BT offline") == 0);
            bt_peer(&ctx, true);
            CHECK(host_bt_saved && app_remote_get(1, &remote) && remote.bt.bonded && remote.bt.peer[5] == 0xB2);
            host_bt_status.state = BT_CONNECTED;
            tick(&ctx, host_time + 1);
            snapshot(&ctx, "20-bt-remote");
            for (unsigned k = 1; k <= REMOTE_KEY_COUNT; ++k) {
                if (k == REMOTE_KEY_MODE || k == REMOTE_KEY_POWER) continue;
                unsigned taps = host_bt_taps;
                key(&ctx, k);
                CHECK(current(&ctx) == UI_SCREEN_REMOTE);
                CHECK(host_bt_taps == taps + ((bt_keymap_mask() >> (k - 1)) & 1));
                release(&ctx, k);
                CHECK(host_bt_taps == taps + ((bt_keymap_mask() >> (k - 1)) & 1));
            }
            unsigned taps = host_bt_taps;
            key(&ctx, REMOTE_KEY_POWER);
            CHECK(host_bt_taps == taps);
            release(&ctx, REMOTE_KEY_POWER);
            CHECK(host_bt_taps == taps + 1 && host_bt_report.data[2] == 0x66);
            key(&ctx, REMOTE_KEY_POWER);
            ui_event_t hold = {.type = UI_EV_POWER_LONG, .src = UI_SRC_KEYPAD, .value = REMOTE_KEY_POWER};
            ui_nav_dispatch(&ctx, &hold);
            release(&ctx, REMOTE_KEY_POWER);
            CHECK(host_bt_taps == taps + 2 && host_bt_report.data[0] == 5 && host_bt_report.data[2] == 0x4C);
            key(&ctx, REMOTE_KEY_POWER); // Switching mode cancels a pending short Power.
            key(&ctx, 2);
            release(&ctx, REMOTE_KEY_POWER);
            CHECK(current(&ctx) == UI_SCREEN_BT_DEVICE && host_bt_taps == taps + 2);
            CHECK(host_bt_opens == 1 && host_bt_closes == 0);
            snapshot(&ctx, "21-bt-connected");
            host_bt_status.state = BT_SUSPENDED;
            key(&ctx, 2); key(&ctx, REMOTE_KEY_OK);
            CHECK(host_bt_taps == taps + 3); // Explicit input can wake a suspended HID host.
            key(&ctx, 2);
            host_bt_status.state = BT_CONNECTED;
            ctx.bt_menu_sel = 3; key(&ctx, 7);
            CHECK(current(&ctx) == UI_SCREEN_BT_KEYS);
            snapshot(&ctx, "22-bt-map");
            key(&ctx, 10);
            ctx.bt_menu_sel = 1; key(&ctx, 7);
            CHECK(current(&ctx) == UI_SCREEN_BT_CONFIRM && ctx.menu_sel == 0);
            snapshot(&ctx, "23-bt-confirm");
            key(&ctx, 7); // Cancel preserves the bond.
            CHECK(host_bt_forgets == 0);
            CHECK(app_remote_get(1, &remote) && memcmp(paired_identity, remote.bt.local_addr, 6) == 0);
            ctx.bt_menu_sel = 2; key(&ctx, 7); key(&ctx, 9); key(&ctx, 7);
            CHECK(host_bt_forgets == 1 && !host_bt_pair_after);
            bt_peer(&ctx, false);
            CHECK(app_remote_get(1, &remote) && !remote.bt.bonded && remote.bt.bond_store == BT_BOND_STORE_PROFILE);
            CHECK(remote_bt_has_identity(&remote) && memcmp(paired_identity, remote.bt.local_addr, 6) == 0);
            ctx.bt_menu_sel = 1; key(&ctx, 7); key(&ctx, 9); key(&ctx, 7);
            CHECK(host_bt_forgets == 2 && host_bt_pair_after);
            bt_peer(&ctx, true);
            host_fail_save = 1;
            bt_peer(&ctx, false);
            CHECK(!host_bt_saved && app_remote_get(1, &remote) && remote.bt.bonded);
            host_fail_save = 0;
            key(&ctx, 10);
            CHECK(host_bt_closes == 1);
        } else {
            key(&ctx, 2);
            CHECK(current(&ctx) == UI_SCREEN_CONSTRUCTION);
            key(&ctx, 7);
        }
        CHECK(current(&ctx) == UI_SCREEN_HOME);
    }
    CHECK(app_remote_count() == 3);
    ctx.home_sel = 1;
    snapshot(&ctx, "17-home-list");
    key(&ctx, 2);
    CHECK(current(&ctx) == UI_SCREEN_HOME && host_bt_opens == 1); // BT must be opened first.
    key(&ctx, 7);
    CHECK(current(&ctx) == UI_SCREEN_BT_DEVICE && host_bt_opens == 2);
    CHECK(host_bt_status.state == BT_RECONNECTING);
    uint32_t old_session = ctx.bt_session;
    app_evt_t late_bt = {.type = APP_EVT_BT_PEER, .request_id = old_session - 1};
    ui_handle_app_event(&ctx, &late_bt);
    CHECK(app_remote_get(1, &remote) && remote.bt.bonded);
    ctx.bt_menu_sel = 4; key(&ctx, 7);
    key(&ctx, 9); key(&ctx, 9); key(&ctx, 9); key(&ctx, 7);
    CHECK(current(&ctx) == UI_SCREEN_REMOVE && ctx.menu_sel == 0);
    snapshot(&ctx, "18-remove");
    key(&ctx, 7);
    CHECK(current(&ctx) == UI_SCREEN_EDIT && app_remote_count() == 3);
    key(&ctx, 7); key(&ctx, 9);
    host_fail_save = 1;
    key(&ctx, 7);
    bt_peer(&ctx, false);
    CHECK(app_remote_count() == 3 && current(&ctx) == UI_SCREEN_REMOVE);
    host_fail_save = 0;
    key(&ctx, 7);
    bt_peer(&ctx, false);
    CHECK(app_remote_count() == 2 && current(&ctx) == UI_SCREEN_HOME);
    CHECK(host_bt_closes == 2 && !ctx.bt_session);
    CHECK(app_remote_get(1, &remote) && strcmp(remote.name, "WiFi Light") == 0);
    app_state_init();
    CHECK(app_remote_count() == 2);
    remote_t race = {.name = "Pair race", .transport = REMOTE_BT};
    CHECK(app_remote_create(&race, &index) == ESP_OK && index == 2);
    ctx.home_sel = index;
    key(&ctx, 7);
    CHECK(current(&ctx) == UI_SCREEN_BT_DEVICE);
    ctx.bt_menu_sel = 4; key(&ctx, 7);
    key(&ctx, 9); key(&ctx, 7);
    CHECK(current(&ctx) == UI_SCREEN_NAME);
    bt_peer(&ctx, true); // Pairing completes after the editor captured its draft.
    strcpy(ctx.draft.name, "Renamed BT");
    ctx.keyboard_sel = 29; key(&ctx, 7);
    CHECK(app_remote_get(index, &remote) && remote.bt.bonded && remote.bt.peer[5] == 0xB2);
    key(&ctx, 9); key(&ctx, 7);
    CHECK(current(&ctx) == UI_SCREEN_ICON);
    bt_peer(&ctx, false);
    ctx.icon_sel = 7; key(&ctx, 7);
    CHECK(app_remote_get(index, &remote) && !remote.bt.bonded && remote.icon == 7);
    uint32_t exited_session = ctx.bt_session;
    unsigned closes = host_bt_closes;
    key(&ctx, REMOTE_KEY_HOME);
    CHECK(current(&ctx) == UI_SCREEN_HOME && !ctx.bt_session && host_bt_closes == closes + 1);
    app_evt_t after_exit = {.type = APP_EVT_BT_PEER, .request_id = exited_session, .bt.bonded = true};
    ui_handle_app_event(&ctx, &after_exit);
    CHECK(app_remote_get(index, &remote) && !remote.bt.bonded);
    app_state_init();
    CHECK(app_remote_get(index, &remote) && strcmp(remote.name, "Renamed BT") == 0 && !remote.bt.bonded);
    ctx.home_sel = app_remote_count();
    key(&ctx, 7); key(&ctx, 9); key(&ctx, 7); key(&ctx, 7);
    CHECK(current(&ctx) == UI_SCREEN_NAME && ctx.draft.transport == REMOTE_BT && ctx.draft.assigned);
    key(&ctx, 10); key(&ctx, 10); key(&ctx, 5); key(&ctx, 7);
    CHECK(current(&ctx) == UI_SCREEN_NAME && ctx.draft.transport == REMOTE_IR && ctx.draft.assigned == 0);
    for (unsigned k = 0; k < REMOTE_KEY_COUNT; ++k)
        CHECK(ctx.draft.keys[k].addr == 0 && ctx.draft.keys[k].cmd == 0);
    key(&ctx, REMOTE_KEY_HOME);
    CHECK(app_remote_count() == 3);
    CHECK(bluetooth_preset_checks(&ctx) == 0);
    printf("PASS: %u behavior checks; portrait screen snapshots saved in build_host.\n", checks);
    return 0;
}
