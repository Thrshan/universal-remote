#pragma once
#include <stdint.h>
#include <stdbool.h>

#define REMOTE_KEY_COUNT 21
#define REMOTE_NAME_MAX 12
#define REMOTE_ICON_COUNT 24
#define MAX_REMOTES 16
#define BT_TEXT_MAX 64

// Values are the physical key numbers in the PCB photograph.
typedef enum {
    REMOTE_KEY_POWER = 1, REMOTE_KEY_MODE, REMOTE_KEY_CUSTOM2,
    REMOTE_KEY_CUSTOM3, REMOTE_KEY_UP, REMOTE_KEY_LEFT, REMOTE_KEY_OK,
    REMOTE_KEY_RIGHT, REMOTE_KEY_DOWN, REMOTE_KEY_BACK,
    REMOTE_KEY_PLAY_PAUSE, REMOTE_KEY_HOME, REMOTE_KEY_VOL_UP,
    REMOTE_KEY_CH_UP, REMOTE_KEY_VOL_CLICK, REMOTE_KEY_CH_CLICK,
    REMOTE_KEY_VOL_DOWN, REMOTE_KEY_CH_DOWN, REMOTE_KEY_CUSTOM4,
    REMOTE_KEY_CUSTOM5, REMOTE_KEY_CUSTOM6,
} remote_key_t;

typedef enum { REMOTE_IR = 0, REMOTE_BT, REMOTE_WIFI } remote_transport_t;
typedef enum { BT_REMOTE_KEYBOARD = 0, BT_REMOTE_MOUSE, BT_REMOTE_CONTROLLER } bt_remote_kind_t;
typedef enum { BT_BOND_STORE_LEGACY = 0, BT_BOND_STORE_PROFILE = 1 } bt_bond_store_t;

typedef enum { BT_PRESET_FIRE_TV = 0, BT_PRESET_MEDIA, BT_PRESET_TEXT, BT_PRESET_CUSTOM } bt_preset_t;
typedef enum { BT_MOUSE_MAPPED = 0, BT_MOUSE_PATTERNS } bt_mouse_mode_t;
#define BT_MOUSE_SPEED_DEFAULT 5
#define BT_MOUSE_SPEED_MAX 10
typedef struct { uint8_t keycode; uint8_t modifiers; } bt_binding_t;
typedef struct { uint16_t addr; uint16_t cmd; } ir_nec_code_t;
typedef struct {
    char name[REMOTE_NAME_MAX + 1];
    uint8_t transport;
    uint8_t icon;
    uint32_t assigned;
    // Keep the v1 profile size/layout: BT metadata occupies the unused IR slots.
    union {
        ir_nec_code_t keys[REMOTE_KEY_COUNT];
        struct {
            uint8_t kind;
            uint8_t bonded;
            uint8_t addr_type;
            uint8_t peer[6];
            uint8_t local_addr[6]; // Persistent static-random BLE identity for this profile.
            uint8_t preset; // Zero keeps existing profiles on the Fire TV preset.
            bt_binding_t bindings[REMOTE_KEY_COUNT];
            uint8_t bond_store; // Versioned backend: legacy shared store or per-identity store.
            uint8_t mouse_mode;
            uint8_t mouse_speed; // 1..10; zero from older profiles uses the default.
        } bt;
    };
} remote_t;

const char *remote_key_name(unsigned key);
const char *remote_transport_name(unsigned transport);
bool remote_key_assignable(unsigned key);
bool remote_bt_has_identity(const remote_t *remote);
void remote_bt_ensure_identity(remote_t *remote);
unsigned remote_assigned_count(const remote_t *remote);
bool remote_valid(const remote_t *remote);
