#include "ui_nav.h"
#include "u8g2.h"
#include "oled_port.h"
#include "host_support.h"
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

int main(void)
{
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

    // BT/WIFI are saved at the icon step, then show their selected Exit action.
    key(&ctx, 12);
    for (unsigned transport = 1; transport <= 2; ++transport) {
        ctx.home_sel = app_remote_count();
        key(&ctx, 7);
        for (unsigned i = 0; i < transport; ++i) key(&ctx, 9);
        key(&ctx, 7);
        strcpy(ctx.draft.name, transport == 1 ? "BT Speaker" : "WiFi Light");
        ctx.keyboard_sel = 29;
        key(&ctx, 7);
        ctx.icon_sel = transport == 1 ? 2 : 5;
        key(&ctx, 7);
        CHECK(current(&ctx) == UI_SCREEN_CONSTRUCTION);
        snapshot(&ctx, transport == 1 ? "15-bluetooth" : "16-wifi");
        key(&ctx, 2);
        CHECK(current(&ctx) == UI_SCREEN_CONSTRUCTION);
        key(&ctx, 7);
        CHECK(current(&ctx) == UI_SCREEN_HOME);
    }
    CHECK(app_remote_count() == 3);
    ctx.home_sel = 1;
    snapshot(&ctx, "17-home-list");
    key(&ctx, 7);
    key(&ctx, 9); key(&ctx, 9); key(&ctx, 9); key(&ctx, 7);
    CHECK(current(&ctx) == UI_SCREEN_REMOVE && ctx.menu_sel == 0);
    snapshot(&ctx, "18-remove");
    key(&ctx, 7);
    CHECK(current(&ctx) == UI_SCREEN_EDIT && app_remote_count() == 3);
    key(&ctx, 7); key(&ctx, 9);
    host_fail_save = 1;
    key(&ctx, 7);
    CHECK(app_remote_count() == 3 && current(&ctx) == UI_SCREEN_REMOVE);
    host_fail_save = 0;
    key(&ctx, 7);
    CHECK(app_remote_count() == 2 && current(&ctx) == UI_SCREEN_HOME);
    CHECK(app_remote_get(1, &remote) && strcmp(remote.name, "WiFi Light") == 0);
    app_state_init();
    CHECK(app_remote_count() == 2);
    printf("PASS: %u behavior checks; portrait screen snapshots saved in build_host.\n", checks);
    return 0;
}
