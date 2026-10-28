#include "remote_model.h"
#include "bt_keymap.h"
#include "bt_mouse.h"
#include "esp_random.h"
#include <string.h>

static const char *const key_names[] = {
    "", "POWER", "MODE", "CUST BTN 2", "CUST BTN 3", "UP", "LEFT", "OK",
    "RIGHT", "DOWN", "BACK", "PLAY/PAUSE", "HOME", "VOL UP", "CH UP",
    "VOL CLICK", "CH CLICK", "VOL DOWN", "CH DOWN", "CUST BTN 4",
    "CUST BTN 5", "CUST BTN 6",
};

const char *remote_key_name(unsigned key)
{ return key <= REMOTE_KEY_COUNT ? key_names[key] : ""; }

const char *remote_transport_name(unsigned transport)
{
    static const char *const names[] = {"IR", "BT", "WIFI"};
    return transport <= REMOTE_WIFI ? names[transport] : "?";
}

bool remote_key_assignable(unsigned key)
{ return key >= 1 && key <= REMOTE_KEY_COUNT && key != REMOTE_KEY_MODE; }

bool remote_bt_has_identity(const remote_t *remote)
{
    const uint8_t *addr = remote->bt.local_addr;
    if ((addr[0] & 0xC0) != 0xC0) return false;
    bool all_zero = (addr[0] & 0x3F) == 0, all_one = (addr[0] & 0x3F) == 0x3F;
    for (unsigned i = 1; i < 6; ++i) {
        all_zero &= addr[i] == 0;
        all_one &= addr[i] == 0xFF;
    }
    return !all_zero && !all_one;
}

void remote_bt_ensure_identity(remote_t *remote)
{
    if (remote_bt_has_identity(remote)) return;
    do {
        esp_fill_random(remote->bt.local_addr, 6);
        remote->bt.local_addr[0] |= 0xC0;
    } while (!remote_bt_has_identity(remote));
}

unsigned remote_assigned_count(const remote_t *remote)
{
    unsigned count = 0;
    uint32_t assigned = remote->transport == REMOTE_BT && remote->bt.kind == BT_REMOTE_MOUSE
        ? bt_mouse_mask(remote->bt.mouse_mode) : remote->transport == REMOTE_BT && remote->bt.kind == BT_REMOTE_KEYBOARD
        ? bt_profile_mask(remote) : remote->assigned;
    for (unsigned key = 1; key <= REMOTE_KEY_COUNT; ++key)
        if (remote_key_assignable(key) && (assigned & (1u << (key - 1)))) ++count;
    return count;
}

bool remote_valid(const remote_t *remote)
{
    const uint32_t allowed = ((1u << REMOTE_KEY_COUNT) - 1u) & ~(1u << (REMOTE_KEY_MODE - 1));
    if (remote->transport == REMOTE_BT) {
        if (remote->bt.preset > BT_PRESET_CUSTOM || remote->bt.bond_store > BT_BOND_STORE_PROFILE) return false;
        if (remote->bt.kind == BT_REMOTE_MOUSE
            && (remote->bt.mouse_mode > BT_MOUSE_PATTERNS || remote->bt.mouse_speed > BT_MOUSE_SPEED_MAX)) return false;
        if (remote->bt.kind == BT_REMOTE_KEYBOARD && remote->bt.preset == BT_PRESET_CUSTOM)
            for (unsigned i = 0; i < REMOTE_KEY_COUNT; ++i)
                if (!bt_binding_valid(remote->bt.bindings[i])
                    || (i == REMOTE_KEY_MODE - 1 && remote->bt.bindings[i].keycode)) return false;
    }
    return remote->name[0] && memchr(remote->name, 0, sizeof(remote->name))
        && remote->transport <= REMOTE_WIFI && remote->icon < REMOTE_ICON_COUNT
        && !(remote->assigned & ~allowed)
        && (remote->transport != REMOTE_BT || (remote->bt.kind <= BT_REMOTE_CONTROLLER
            && remote->bt.bonded <= 1 && remote->bt.addr_type <= 3));
}
