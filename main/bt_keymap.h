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
extern const uint8_t bt_keyboard_report_map[];
extern const size_t bt_keyboard_report_map_size;
