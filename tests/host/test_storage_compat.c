// These fixtures were produced by the pre-reorganization firmware sources.
// Compare durable bytes, rather than accepting a matching writer/reader bug.
#include "app_state.h"
#include "host_support.h"
#include "nvs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned checks, snapshots;
#define CHECK(value) do { ++checks; if (!(value)) { fprintf(stderr, "Storage compatibility failed at line %d: %s\n", __LINE__, #value); exit(1); } } while (0)
static const char *fixture_dir = "tests/host/fixtures/storage";
static bool record;

static void snapshot(const char *name)
{
    uint8_t actual[32768], expected[32768];
    size_t size = sizeof(actual);
    nvs_handle_t handle;
    CHECK(nvs_open("remotes", NVS_READONLY, &handle) == ESP_OK);
    CHECK(nvs_get_blob(handle, "profiles", actual, &size) == ESP_OK);
    nvs_close(handle);
    char path[512];
    snprintf(path, sizeof(path), "%s/%s.bin", fixture_dir, name);
    FILE *file = fopen(path, record ? "wb" : "rb");
    CHECK(file != NULL);
    if (record) {
        CHECK(fwrite(actual, 1, size, file) == size);
    } else {
        size_t length = fread(expected, 1, sizeof(expected), file);
        CHECK(length == size);
        CHECK(memcmp(actual, expected, size) == 0);
    }
    CHECK(fclose(file) == 0);
    ++snapshots;
}

static remote_t keyboard(const char *name, unsigned preset, unsigned identity)
{
    remote_t remote = {0};
    snprintf(remote.name, sizeof(remote.name), "%s", name);
    remote.transport = REMOTE_BT;
    remote.icon = 3;
    remote.bt.kind = BT_REMOTE_KEYBOARD;
    remote.bt.preset = preset;
    remote.bt.bond_store = BT_BOND_STORE_PROFILE;
    const uint8_t local[] = {0xC2, 0x12, 0x34, 0x56, 0x78, identity};
    const uint8_t peer[] = {0x11, 0x22, 0x33, 0x44, 0x55, identity};
    memcpy(remote.bt.local_addr, local, 6);
    memcpy(remote.bt.peer, peer, 6);
    remote.bt.bonded = 1;
    remote.bt.addr_type = 1;
    return remote;
}

int main(int argc, char **argv)
{
    if (argc == 3 && strcmp(argv[1], "--record") == 0) {
        record = true;
        fixture_dir = argv[2];
    } else {
        CHECK(argc == 1);
    }
    host_clear_storage();
    app_state_init();
    remote_t ir = {0};
    strcpy(ir.name, "Living TV");
    ir.icon = 5;
    CHECK(app_remote_create(&ir, NULL) == ESP_OK);
    for (unsigned key = 1; key <= REMOTE_KEY_COUNT; ++key) {
        if (key == REMOTE_KEY_MODE) continue;
        ir_nec_code_t code = {.addr = 0xAB00u + key, .cmd = 0xCD00u + key};
        CHECK(app_remote_set_code(0, key, code) == ESP_OK);
    }
    for (unsigned preset = BT_PRESET_FIRE_TV; preset <= BT_PRESET_CUSTOM; ++preset) {
        static const char *names[] = {"Fire TV", "Media", "Text", "Custom"};
        remote_t remote = keyboard(names[preset], preset, preset + 1);
        CHECK(app_remote_create(&remote, NULL) == ESP_OK);
    }
    CHECK(app_remote_set_bt_text(3, REMOTE_KEY_POWER, "Hello, TV!") == ESP_OK);
    CHECK(app_remote_set_bt_text(3, REMOTE_KEY_BACK, " back ") == ESP_OK);
    CHECK(app_remote_set_bt_text(3, REMOTE_KEY_HOME, "[]{}\\|\"'$%^<>~`-+=:;!?@#&*") == ESP_OK);
    CHECK(app_remote_set_bt_binding(4, REMOTE_KEY_POWER, (bt_binding_t){.keycode = 0x4C, .modifiers = 5}) == ESP_OK);
    CHECK(app_remote_set_bt_binding(4, REMOTE_KEY_HOME, (bt_binding_t){.keycode = 0x04, .modifiers = 2}) == ESP_OK);
    for (unsigned mode = BT_MOUSE_MAPPED; mode <= BT_MOUSE_PATTERNS; ++mode) {
        remote_t remote = keyboard(mode ? "Patterns" : "Mouse", 0, mode + 5);
        remote.bt.kind = BT_REMOTE_MOUSE;
        remote.bt.mouse_mode = mode;
        remote.bt.mouse_speed = mode ? 9 : 3;
        CHECK(app_remote_create(&remote, NULL) == ESP_OK);
    }
    remote_t wifi = {0};
    strcpy(wifi.name, "WiFi");
    wifi.transport = REMOTE_WIFI;
    CHECK(app_remote_create(&wifi, NULL) == ESP_OK);
    snapshot("mixed-v3");

    app_state_init();
    CHECK(app_remote_count() == 8);
    char text[BT_TEXT_MAX + 1];
    CHECK(app_remote_get_bt_text(3, REMOTE_KEY_BACK, text));
    CHECK(strcmp(text, " back ") == 0);
    CHECK(app_remote_get(0, &ir));
    strcpy(ir.name, "Renamed TV");
    CHECK(app_remote_update(0, &ir) == ESP_OK);
    CHECK(app_remote_remove(1) == ESP_OK);
    CHECK(app_remote_set_bt_text(2, REMOTE_KEY_POWER, "Updated") == ESP_OK);
    snapshot("edited-v3");
    host_fail_save = 2;
    CHECK(app_remote_set_bt_text(2, REMOTE_KEY_POWER, "Must rollback") != ESP_OK);
    CHECK(app_remote_remove(0) != ESP_OK);
    host_fail_save = 0;
    snapshot("edited-v3");
    app_state_init();
    CHECK(app_remote_get_bt_text(2, REMOTE_KEY_POWER, text));
    CHECK(strcmp(text, "Updated") == 0);

    for (unsigned version = 1; version <= 2; ++version) {
        host_clear_storage();
        remote_t remote = keyboard("Legacy BT", BT_PRESET_FIRE_TV, 8);
        host_version_storage(&remote, version);
        app_state_init();
        CHECK(app_remote_get(0, &remote));
        CHECK(remote.bt.bond_store == BT_BOND_STORE_LEGACY);
        CHECK(app_remote_update(0, &remote) == ESP_OK);
        snapshot(version == 1 ? "v1-to-v3" : "v2-to-v3");
    }
    host_clear_storage();
    host_legacy_storage(32);
    app_state_init();
    CHECK(app_remote_count() == 2);
    snapshot("legacy-ir-to-v3");
    printf("PASS: %u storage compatibility checks; %u snapshots %s original firmware bytes.\n",
           checks, snapshots, record ? "recorded from" : "match");
    return 0;
}
