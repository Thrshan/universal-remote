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

const char *bt_preset_name(unsigned preset)
{
    static const char *const names[] = {"Fire TV", "Media control", "Text", "Custom"};
    return preset <= BT_PRESET_CUSTOM ? names[preset] : "Unknown";
}

// Keyboard usage IDs are stored directly. The 0x80 range names the Consumer
// usages we support, so each editable binding fits in the existing v1 overlay.
static const uint16_t consumer_usages[] = {
    0x0223, 0x00CD, 0x00E9, 0x00EA, 0x00E2, 0x00B5,
    0x00B6, 0x00B7, 0x00B4, 0x00B3, 0x0030,
};

bool bt_binding_valid(bt_binding_t binding)
{
    unsigned code = binding.keycode;
    return (!code && !binding.modifiers)
        || (code >= 4 && code <= 0x73)
        || (code >= 0xE0 && code <= 0xE7)
        || (code >= BT_KEY_LAUNCHER && code <= BT_KEY_POWER && !binding.modifiers);
}

bool bt_binding_report(bt_binding_t binding, bt_key_report_t *report)
{
    memset(report, 0, sizeof(*report));
    if (!binding.keycode || !bt_binding_valid(binding)) return false;
    report->report_id = 1;
    report->length = 8;
    report->data[0] = binding.modifiers;
    if (binding.keycode >= 0xE0) report->data[0] |= 1u << (binding.keycode - 0xE0);
    else if (binding.keycode >= BT_KEY_LAUNCHER) {
        uint16_t usage = consumer_usages[binding.keycode - BT_KEY_LAUNCHER];
        report->report_id = 2;
        report->length = 2;
        report->data[0] = usage & 0xFF;
        report->data[1] = usage >> 8;
    } else report->data[2] = binding.keycode;
    return true;
}

bt_binding_t bt_profile_binding(const remote_t *remote, unsigned key)
{
    bt_binding_t binding = {0};
    if (!remote_key_assignable(key)) return binding;
    if (remote->bt.preset == BT_PRESET_CUSTOM) return remote->bt.bindings[key - 1];
    if (remote->bt.preset == BT_PRESET_TEXT) return binding;
    if (remote->bt.preset == BT_PRESET_FIRE_TV) {
        bt_key_report_t report;
        if (bt_keymap_report(key, false, &report))
            binding.keycode = report.report_id == 2 ? BT_KEY_LAUNCHER : report.data[2];
        return binding;
    }
    switch (key) {
        case REMOTE_KEY_PLAY_PAUSE: binding.keycode = BT_KEY_PLAY_PAUSE; break;
        case REMOTE_KEY_VOL_UP: binding.keycode = BT_KEY_VOL_UP; break;
        case REMOTE_KEY_VOL_DOWN: binding.keycode = BT_KEY_VOL_DOWN; break;
        case REMOTE_KEY_VOL_CLICK: binding.keycode = BT_KEY_MUTE; break;
        case REMOTE_KEY_CH_UP: binding.keycode = BT_KEY_NEXT; break;
        case REMOTE_KEY_CH_DOWN: binding.keycode = BT_KEY_PREVIOUS; break;
        case REMOTE_KEY_CH_CLICK: binding.keycode = BT_KEY_STOP; break;
        case REMOTE_KEY_CUSTOM2: binding.keycode = BT_KEY_REWIND; break;
        case REMOTE_KEY_CUSTOM3: binding.keycode = BT_KEY_FORWARD; break;
        case REMOTE_KEY_POWER: binding.keycode = BT_KEY_POWER; break;
        case REMOTE_KEY_BACK: binding.keycode = 0x29; break;
        case REMOTE_KEY_HOME: binding.keycode = BT_KEY_LAUNCHER; break;
        case REMOTE_KEY_UP: binding.keycode = 0x52; break;
        case REMOTE_KEY_DOWN: binding.keycode = 0x51; break;
        case REMOTE_KEY_LEFT: binding.keycode = 0x50; break;
        case REMOTE_KEY_RIGHT: binding.keycode = 0x4F; break;
        case REMOTE_KEY_OK: binding.keycode = 0x28; break;
        default: break;
    }
    return binding;
}

bool bt_profile_report(const remote_t *remote, unsigned key, bool long_hold, bt_key_report_t *report)
{
    if (remote->bt.preset == BT_PRESET_FIRE_TV && long_hold)
        return bt_keymap_report(key, true, report);
    if (long_hold) return false;
    return bt_binding_report(bt_profile_binding(remote, key), report);
}

bool bt_profile_key_assigned(const remote_t *remote, unsigned key)
{
    if (!remote_key_assignable(key)) return false;
    return remote->bt.preset == BT_PRESET_TEXT ? (remote->assigned & (1u << (key - 1))) != 0
        : bt_profile_binding(remote, key).keycode != 0;
}

uint32_t bt_profile_mask(const remote_t *remote)
{
    uint32_t mask = 0;
    for (unsigned key = 1; key <= REMOTE_KEY_COUNT; ++key)
        if (bt_profile_key_assigned(remote, key)) mask |= 1u << (key - 1);
    return mask;
}

static const char *const standard_labels[] = {
    "A","B","C","D","E","F","G","H","I","J","K","L","M",
    "N","O","P","Q","R","S","T","U","V","W","X","Y","Z",
    "1","2","3","4","5","6","7","8","9","0",
    "Enter","Esc","Bksp","Tab","Space","-","=","[","]","\\","NonUS#",";","'","`",",",".","/","CapsLk",
    "F1","F2","F3","F4","F5","F6","F7","F8","F9","F10","F11","F12",
    "PrtScr","ScrLk","Pause","Insert","Home","PgUp","Delete","End","PgDn",
    "Right","Left","Down","Up","NumLk","KP /","KP *","KP -","KP +","KP Ent",
    "KP 1","KP 2","KP 3","KP 4","KP 5","KP 6","KP 7","KP 8","KP 9","KP 0","KP .",
    "NonUS\\","Menu","Power","KP =","F13","F14","F15","F16","F17","F18","F19","F20","F21","F22","F23","F24",
};
_Static_assert(sizeof(standard_labels) / sizeof(standard_labels[0]) == 0x73 - 4 + 1,
               "Every standard keyboard usage must have a label");

const char *bt_keycode_label(unsigned code)
{
    static const char *const modifiers[] = {"LCtrl","LShift","LAlt","LGui","RCtrl","RShift","RAlt","RGui"};
    static const char *const media[] = {"Launcher","Play/Pause","Vol +","Vol -","Mute","Next","Prev","Stop","Rewind","Forward","Power"};
    if (code >= 4 && code <= 0x73) return standard_labels[code - 4];
    if (code >= 0xE0 && code <= 0xE7) return modifiers[code - 0xE0];
    if (code >= BT_KEY_LAUNCHER && code <= BT_KEY_POWER) return media[code - BT_KEY_LAUNCHER];
    return "Clear";
}

unsigned bt_keycode_count(void) { return 1 + sizeof(standard_labels) / sizeof(standard_labels[0]) + 8; }
unsigned bt_keycode_at(unsigned index)
{
    if (!index || index >= bt_keycode_count()) return 0;
    if (index <= 0x73 - 3) return index + 3;
    return 0xE0 + index - (0x73 - 3) - 1;
}

bool bt_text_report(char character, bt_key_report_t *report)
{
    unsigned char ch = (unsigned char)character;
    bt_binding_t binding = {0};
    if (ch >= 'a' && ch <= 'z') binding.keycode = 4 + ch - 'a';
    else if (ch >= 'A' && ch <= 'Z') { binding.keycode = 4 + ch - 'A'; binding.modifiers = 2; }
    else if (ch >= '1' && ch <= '9') binding.keycode = 0x1E + ch - '1';
    else if (ch == '0') binding.keycode = 0x27;
    else if (ch == '\n' || ch == '\r') binding.keycode = 0x28;
    else if (ch == '\t') binding.keycode = 0x2B;
    else if (ch == ' ') binding.keycode = 0x2C;
    else {
        static const char plain[] = "-=[]\\;'`,./";
        static const char shifted[] = "_+{}|:\"~<>?";
        static const uint8_t codes[] = {0x2D,0x2E,0x2F,0x30,0x31,0x33,0x34,0x35,0x36,0x37,0x38};
        const char *found = ch ? strchr(plain, ch) : NULL;
        if (found) binding.keycode = codes[found - plain];
        else if (ch && (found = strchr(shifted, ch))) { binding.keycode = codes[found - shifted]; binding.modifiers = 2; }
        else {
            static const char symbols[] = "!@#$%^&*()";
            found = ch ? strchr(symbols, ch) : NULL;
            if (!found) return false;
            binding.keycode = 0x1E + (found - symbols);
            binding.modifiers = 2;
        }
    }
    return bt_binding_report(binding, report);
}

bool bt_text_valid(const char *text)
{
    if (!text) return false;
    bt_key_report_t report;
    for (unsigned i = 0; i <= BT_TEXT_MAX; ++i) {
        if (!text[i]) return true;
        if (i == BT_TEXT_MAX || !bt_text_report(text[i], &report)) return false;
    }
    return false;
}
