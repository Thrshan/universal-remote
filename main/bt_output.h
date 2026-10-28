#pragma once
#include "bt_keymap.h"

typedef struct {
    void *context;
    bool (*active)(void *context);
    bool (*send)(void *context, const bt_key_report_t *report);
    void (*pause)(void *context, unsigned milliseconds);
} bt_output_t;

// Always release an attempted press, even if it fails or playback is cancelled.
bool bt_output_tap(const bt_output_t *output, const bt_key_report_t *report);
bool bt_output_text(const bt_output_t *output, const char *text);
