#pragma once
#include <stddef.h>
#include "app_state.h"

typedef struct {
    uint8_t report_id;
    uint8_t length;
    uint8_t data[8];
} bt_key_report_t;

// A tap is always followed by an all-zero report with the same ID and size.
bool bt_keymap_report(unsigned key, bool long_hold, bt_key_report_t *report);
const char *bt_keymap_label(unsigned key);
uint32_t bt_keymap_mask(void);
enum {
    BT_KEY_LAUNCHER = 0x80, BT_KEY_PLAY_PAUSE, BT_KEY_VOL_UP, BT_KEY_VOL_DOWN,
    BT_KEY_MUTE, BT_KEY_NEXT, BT_KEY_PREVIOUS, BT_KEY_STOP, BT_KEY_REWIND,
    BT_KEY_FORWARD, BT_KEY_POWER,
};
const char *bt_preset_name(unsigned preset);
bt_binding_t bt_profile_binding(const remote_t *remote, unsigned key);
bool bt_binding_valid(bt_binding_t binding);
bool bt_binding_report(bt_binding_t binding, bt_key_report_t *report);
bool bt_profile_report(const remote_t *remote, unsigned key, bool long_hold, bt_key_report_t *report);
bool bt_profile_key_assigned(const remote_t *remote, unsigned key);
uint32_t bt_profile_mask(const remote_t *remote);
const char *bt_keycode_label(unsigned keycode);
unsigned bt_keycode_count(void);
unsigned bt_keycode_at(unsigned index);
bool bt_text_report(char character, bt_key_report_t *report);
bool bt_text_valid(const char *text);
extern const uint8_t bt_keyboard_report_map[];
extern const size_t bt_keyboard_report_map_size;
