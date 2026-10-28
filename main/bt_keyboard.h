#pragma once
#include "bt_keymap.h"
#include "bt_mouse.h"

typedef enum {
    BT_OFF, BT_STARTING, BT_PAIRING, BT_RECONNECTING, BT_CONNECTING,
    BT_CONNECTED, BT_SUSPENDED, BT_STOPPING, BT_UNPAIRED,
    BT_PAIR_TIMEOUT, BT_ERROR,
} bt_status_t;
typedef struct {
    uint32_t session;
    bt_status_t state;
    esp_err_t error;
    const char *step; // Static diagnostic label; safe to copy with the snapshot.
    unsigned mouse_movement;
    uint8_t mouse_buttons;
} bt_snapshot_t;

void bt_keyboard_init(void); // Starts a worker; the radio remains off.
bool bt_keyboard_open(const remote_t *remote, unsigned profile_index, uint32_t session);
void bt_keyboard_close(void);
// Forget only this profile's peer. Acknowledge durable storage before pairing.
bool bt_keyboard_forget(uint32_t session, bool pair_after);
void bt_keyboard_peer_saved(uint32_t session, bool success, const remote_t *remote);
bool bt_keyboard_tap(uint32_t session, uint32_t request, unsigned key, bool long_hold);
void bt_keyboard_release(uint32_t session);
// Keyboard and mouse share the same profile-scoped Bluetooth worker.
bool bt_keyboard_mouse_input(uint32_t session, unsigned key, bt_mouse_input_t input, uint32_t time_ms);
bt_snapshot_t bt_keyboard_snapshot(void);
static inline const char *bt_keyboard_status_name(bt_status_t state)
{
    static const char *const names[] = {"BT off", "Starting BT", "BT searching",
        "Reconnecting", "Connecting", "Connected", "BT suspended", "Stopping BT",
        "Unpaired", "Pair timeout", "BT error"};
    return (unsigned)state <= BT_ERROR ? names[state] : "BT error";
}
