#pragma once
#include "bt_output.h"

#define BT_MOUSE_REPORT_ID 1
#define BT_MOUSE_REPORT_SIZE 5
#define BT_MOUSE_INTERVAL_MS 20
#define BT_MOUSE_HOLD_MS 700

extern const uint8_t bt_mouse_report_map[];
extern const size_t bt_mouse_report_map_size;
typedef enum { BT_MOUSE_PRESS, BT_MOUSE_RELEASE, BT_MOUSE_HOLD, BT_MOUSE_REPEAT } bt_mouse_input_t;

typedef struct {
    uint8_t buttons;
    unsigned movement, pressed_pattern;
    bool same_pattern, momentary, ok_pending, ok_long;
    uint32_t pressed_at, next_tick, next_turn, random;
    int vx, vy, target_x, target_y, fraction_x, fraction_y;
} bt_mouse_t;

unsigned bt_mouse_speed(const remote_t *remote);
const char *bt_mouse_mode_name(unsigned mode);
const char *bt_mouse_key_label(unsigned mode, unsigned key);
uint32_t bt_mouse_mask(unsigned mode);
void bt_mouse_reset(bt_mouse_t *mouse, uint32_t seed);
bool bt_mouse_input(bt_mouse_t *mouse, const remote_t *remote, unsigned key,
                    bt_mouse_input_t input, uint32_t now, const bt_output_t *output);
bool bt_mouse_tick(bt_mouse_t *mouse, const remote_t *remote, uint32_t now, const bt_output_t *output);
