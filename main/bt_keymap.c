#include "bt_keymap.h"
#include <string.h>

// Eight-byte boot-compatible keyboard input (ID 1), LED output, and a
// 16-bit Consumer Control input (ID 2) for Android's launcher Home command.
const uint8_t bt_keyboard_report_map[] = {
    0x05,0x01, 0x09,0x06, 0xA1,0x01, 0x85,0x01,
    0x05,0x07, 0x19,0xE0, 0x29,0xE7, 0x15,0x00, 0x25,0x01,
    0x75,0x01, 0x95,0x08, 0x81,0x02,
    0x75,0x08, 0x95,0x01, 0x81,0x03,
    0x05,0x08, 0x19,0x01, 0x29,0x05, 0x75,0x01, 0x95,0x05, 0x91,0x02,
    0x75,0x03, 0x95,0x01, 0x91,0x03,
    0x05,0x07, 0x19,0x00, 0x29,0x73, 0x15,0x00, 0x25,0x73,
    0x75,0x08, 0x95,0x06, 0x81,0x00, 0xC0,
    0x05,0x0C, 0x09,0x01, 0xA1,0x01, 0x85,0x02,
    0x15,0x00, 0x26,0xFF,0x03, 0x19,0x00, 0x2A,0xFF,0x03,
    0x75,0x10, 0x95,0x01, 0x81,0x00, 0xC0,
};
const size_t bt_keyboard_report_map_size = sizeof(bt_keyboard_report_map);

bool bt_keymap_report(unsigned key, bool long_hold, bt_key_report_t *report)
{
    memset(report, 0, sizeof(*report));
    report->report_id = 1;
    report->length = 8;
    if (long_hold) {
        if (key != REMOTE_KEY_POWER) return false;
        report->data[0] = 0x05; // Left Ctrl + Left Alt
        report->data[2] = 0x4C; // Delete
        return true;
    }
    switch (key) {
        case REMOTE_KEY_BACK:       report->data[2] = 0x29; break; // Esc
        case REMOTE_KEY_CUSTOM4:    report->data[2] = 0x65; break; // Application/Menu
        case REMOTE_KEY_CH_UP:      report->data[2] = 0x40; break; // F7
        case REMOTE_KEY_PLAY_PAUSE: report->data[2] = 0x41; break; // F8
        case REMOTE_KEY_CH_DOWN:    report->data[2] = 0x42; break; // F9
        case REMOTE_KEY_UP:         report->data[2] = 0x52; break;
        case REMOTE_KEY_DOWN:       report->data[2] = 0x51; break;
        case REMOTE_KEY_LEFT:       report->data[2] = 0x50; break;
        case REMOTE_KEY_RIGHT:      report->data[2] = 0x4F; break;
        case REMOTE_KEY_OK:         report->data[2] = 0x28; break;
        case REMOTE_KEY_POWER:      report->data[2] = 0x66; break; // PC Power
        case REMOTE_KEY_HOME:
            report->report_id = 2;
            report->length = 2;
            report->data[0] = 0x23; report->data[1] = 0x02; // Consumer AC Home
            break;
        default: return false;
    }
    return true;
}
const char *bt_keymap_label(unsigned key)
{
    switch (key) {
        case REMOTE_KEY_BACK: return "Esc";
        case REMOTE_KEY_HOME: return "Home (house)";
        case REMOTE_KEY_CUSTOM4: return "Menu";
        case REMOTE_KEY_CH_UP: return "F7";
        case REMOTE_KEY_PLAY_PAUSE: return "F8";
        case REMOTE_KEY_CH_DOWN: return "F9";
        case REMOTE_KEY_UP: return "Up arrow";
        case REMOTE_KEY_DOWN: return "Down arrow";
        case REMOTE_KEY_LEFT: return "Left arrow";
        case REMOTE_KEY_RIGHT: return "Right arrow";
        case REMOTE_KEY_OK: return "Enter";
        case REMOTE_KEY_POWER: return "PC Power";
        default: return "Unassigned";
    }
}
uint32_t bt_keymap_mask(void)
{
    uint32_t mask = 0;
    bt_key_report_t report;
    for (unsigned key = 1; key <= REMOTE_KEY_COUNT; ++key)
        if (bt_keymap_report(key, false, &report)) mask |= 1u << (key - 1);
    return mask;
}
