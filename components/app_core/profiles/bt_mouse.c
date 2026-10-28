#include "bt_mouse.h"
#include <string.h>

// Five buttons, relative X/Y and vertical Wheel, then Consumer AC Pan for
// horizontal scroll (USB HID Usage Tables, https://www.usb.org/hid).
const uint8_t bt_mouse_report_map[] = {
    0x05,0x01, 0x09,0x02, 0xA1,0x01, 0x85,BT_MOUSE_REPORT_ID,
    0x09,0x01, 0xA1,0x00,
    0x05,0x09, 0x19,0x01, 0x29,0x05, 0x15,0x00, 0x25,0x01,
    0x75,0x01, 0x95,0x05, 0x81,0x02,
    0x75,0x03, 0x95,0x01, 0x81,0x03,
    0x05,0x01, 0x09,0x30, 0x09,0x31, 0x09,0x38,
    0x15,0x81, 0x25,0x7F, 0x75,0x08, 0x95,0x03, 0x81,0x06,
    0x05,0x0C, 0x0A,0x38,0x02, 0x95,0x01, 0x81,0x06,
    0xC0, 0xC0,
};
const size_t bt_mouse_report_map_size = sizeof(bt_mouse_report_map);

unsigned bt_mouse_speed(const remote_t *remote)
{ return remote->bt.mouse_speed >= 1 && remote->bt.mouse_speed <= BT_MOUSE_SPEED_MAX
    ? remote->bt.mouse_speed : BT_MOUSE_SPEED_DEFAULT; }
const char *bt_mouse_mode_name(unsigned mode)
{ return mode == BT_MOUSE_PATTERNS ? "Patterns" : "Mouse mapped"; }
static bool direction(unsigned key)
{ return key == REMOTE_KEY_UP || key == REMOTE_KEY_DOWN || key == REMOTE_KEY_LEFT || key == REMOTE_KEY_RIGHT; }
static bool pattern(unsigned key)
{ return direction(key) || key == REMOTE_KEY_CUSTOM4; }
const char *bt_mouse_key_label(unsigned mode, unsigned key)
{
    switch (key) {
        case REMOTE_KEY_UP: return "Move up";
        case REMOTE_KEY_DOWN: return "Move down";
        case REMOTE_KEY_LEFT: return "Move left";
        case REMOTE_KEY_RIGHT: return "Move right";
        case REMOTE_KEY_OK: return mode == BT_MOUSE_PATTERNS ? "Left hold on/off" : "Left click";
        case REMOTE_KEY_VOL_UP: return mode == BT_MOUSE_PATTERNS ? "Speed +" : "Scroll up";
        case REMOTE_KEY_VOL_DOWN: return mode == BT_MOUSE_PATTERNS ? "Speed -" : "Scroll down";
        case REMOTE_KEY_CUSTOM4: return mode == BT_MOUSE_PATTERNS ? "Organic motion" : "Speed +";
        case REMOTE_KEY_CUSTOM5: if (mode == BT_MOUSE_MAPPED) return "Speed -"; break;
        case REMOTE_KEY_PLAY_PAUSE: if (mode == BT_MOUSE_MAPPED) return "Right click"; break;
        case REMOTE_KEY_VOL_CLICK: if (mode == BT_MOUSE_MAPPED) return "Middle click"; break;
        case REMOTE_KEY_CH_UP: if (mode == BT_MOUSE_MAPPED) return "Scroll right"; break;
        case REMOTE_KEY_CH_DOWN: if (mode == BT_MOUSE_MAPPED) return "Scroll left"; break;
        case REMOTE_KEY_CUSTOM2: if (mode == BT_MOUSE_MAPPED) return "Mouse Back"; break;
        case REMOTE_KEY_CUSTOM3: if (mode == BT_MOUSE_MAPPED) return "Mouse Forward"; break;
    }
    return "Unassigned";
}
uint32_t bt_mouse_mask(unsigned mode)
{
    uint32_t mask = 0;
    for (unsigned key = 1; key <= REMOTE_KEY_COUNT; ++key)
        if (strcmp(bt_mouse_key_label(mode, key), "Unassigned")) mask |= 1u << (key - 1);
    return mask;
}
void bt_mouse_reset(bt_mouse_t *mouse, uint32_t seed)
{
    memset(mouse, 0, sizeof(*mouse));
    mouse->random = seed ? seed : 0x9E3779B9;
}
static bool send_mouse(bt_mouse_t *mouse, const bt_output_t *output, int x, int y, int wheel, int pan)
{
    bt_key_report_t report = {.report_id = BT_MOUSE_REPORT_ID, .length = BT_MOUSE_REPORT_SIZE,
        .data = {mouse->buttons, (uint8_t)x, (uint8_t)y, (uint8_t)wheel, (uint8_t)pan}};
    return output->send(output->context, &report);
}
static bool click(bt_mouse_t *mouse, const bt_output_t *output, uint8_t button)
{
    uint8_t held = mouse->buttons;
    mouse->buttons |= button;
    bool pressed = send_mouse(mouse, output, 0, 0, 0, 0);
    output->pause(output->context, 40);
    mouse->buttons = held;
    bool released = send_mouse(mouse, output, 0, 0, 0, 0);
    return pressed && released;
}
bool bt_mouse_input(bt_mouse_t *mouse, const remote_t *remote, unsigned key,
                    bt_mouse_input_t input, uint32_t now, const bt_output_t *output)
{
    if (remote->bt.mouse_mode == BT_MOUSE_PATTERNS) {
        if (key == REMOTE_KEY_OK && input == BT_MOUSE_PRESS) {
            mouse->buttons ^= 1;
            return send_mouse(mouse, output, 0, 0, 0, 0);
        }
        if (!pattern(key)) return true;
        if (input == BT_MOUSE_PRESS) {
            mouse->pressed_pattern = key;
            mouse->pressed_at = now;
            mouse->same_pattern = mouse->movement == key;
            mouse->momentary = false;
            if (mouse->same_pattern) mouse->movement = 0;
            if (!mouse->same_pattern) {
                mouse->movement = key;
                mouse->next_tick = now;
                mouse->next_turn = now;
                mouse->vx = mouse->vy = mouse->fraction_x = mouse->fraction_y = 0;
            }
        } else if (key == mouse->pressed_pattern && input == BT_MOUSE_HOLD) {
            mouse->momentary = true;
            mouse->movement = key;
        } else if (key == mouse->pressed_pattern && input == BT_MOUSE_RELEASE) {
            if (mouse->momentary || mouse->same_pattern || (uint32_t)(now - mouse->pressed_at) >= BT_MOUSE_HOLD_MS)
                mouse->movement = 0;
            mouse->pressed_pattern = 0;
        }
        return true;
    }
    if (key == REMOTE_KEY_OK) {
        if (input == BT_MOUSE_PRESS) { mouse->ok_pending = true; mouse->ok_long = false; }
        if (input == BT_MOUSE_HOLD && mouse->ok_pending) {
            mouse->ok_long = true;
            mouse->buttons |= 1;
            return send_mouse(mouse, output, 0, 0, 0, 0);
        }
        if (input == BT_MOUSE_RELEASE && mouse->ok_pending) {
            mouse->ok_pending = false;
            if (mouse->ok_long) return true;
            if (mouse->buttons & 1) {
                mouse->buttons &= ~1;
                return send_mouse(mouse, output, 0, 0, 0, 0);
            }
            return click(mouse, output, 1);
        }
        return true;
    }
    if (direction(key)) {
        if (input == BT_MOUSE_PRESS) { mouse->movement = key; mouse->next_tick = now; }
        if (input == BT_MOUSE_RELEASE && mouse->movement == key) mouse->movement = 0;
        return true;
    }
    if (input != BT_MOUSE_PRESS && input != BT_MOUSE_REPEAT) return true;
    switch (key) {
        case REMOTE_KEY_VOL_UP: return send_mouse(mouse, output, 0, 0, 1, 0);
        case REMOTE_KEY_VOL_DOWN: return send_mouse(mouse, output, 0, 0, -1, 0);
        case REMOTE_KEY_CH_UP: return send_mouse(mouse, output, 0, 0, 0, 1);
        case REMOTE_KEY_CH_DOWN: return send_mouse(mouse, output, 0, 0, 0, -1);
        case REMOTE_KEY_PLAY_PAUSE: return click(mouse, output, 2);
        case REMOTE_KEY_VOL_CLICK: return click(mouse, output, 4);
        case REMOTE_KEY_CUSTOM2: return click(mouse, output, 8);
        case REMOTE_KEY_CUSTOM3: return click(mouse, output, 16);
        default: return true;
    }
}
static uint32_t random_value(bt_mouse_t *mouse)
{
    uint32_t value = mouse->random;
    value ^= value << 13; value ^= value >> 17; value ^= value << 5;
    return mouse->random = value;
}
bool bt_mouse_tick(bt_mouse_t *mouse, const remote_t *remote, uint32_t now, const bt_output_t *output)
{
    if (!mouse->movement || (int32_t)(now - mouse->next_tick) < 0) return true;
    // Do not replay overdue deltas after a busy worker or a reconnect.
    mouse->next_tick = now + BT_MOUSE_INTERVAL_MS;
    int step = (int)bt_mouse_speed(remote) * 2;
    int x = 0, y = 0;
    switch (mouse->movement) {
        case REMOTE_KEY_UP: y = -step; break;
        case REMOTE_KEY_DOWN: y = step; break;
        case REMOTE_KEY_LEFT: x = -step; break;
        case REMOTE_KEY_RIGHT: x = step; break;
        case REMOTE_KEY_CUSTOM4:
            if ((int32_t)(now - mouse->next_turn) >= 0) {
                uint32_t random = random_value(mouse);
                // Curved strokes, changes of direction/speed and occasional rests.
                mouse->target_x = (int)(random & 511) - 256;
                mouse->target_y = (int)((random >> 9) & 511) - 256;
                if ((random >> 18) % 8 == 0) mouse->target_x = mouse->target_y = 0;
                mouse->next_turn = now + 180 + random_value(mouse) % 1120;
            }
            mouse->vx += (mouse->target_x - mouse->vx) / 10;
            mouse->vy += (mouse->target_y - mouse->vy) / 10;
            mouse->fraction_x += mouse->vx * step;
            mouse->fraction_y += mouse->vy * step;
            x = mouse->fraction_x / 256; y = mouse->fraction_y / 256;
            mouse->fraction_x -= x * 256; mouse->fraction_y -= y * 256;
            break;
    }
    return (!x && !y) || send_mouse(mouse, output, x, y, 0, 0);
}
