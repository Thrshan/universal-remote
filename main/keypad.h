#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#include "ui_events.h"

void start_keypad_task(void *ui_q);

// typedef enum {
//     KEYPAD_NUM_1 = 0x0000,
//     KEYPAD_NUM_2 = 0x0001,
//     KEYPAD_NUM_3 = 0x0002,
//     KEYPAD_NUM_4 = 0x0200,
//     KEYPAD_NUM_5 = 0x0201,
//     KEYPAD_NUM_6 = 0x0202,
//     KEYPAD_UP = 0x0001,
//     KEYPAD_DOWM = 0x0201,
//     KEYPAD_ENTER = 0x0202,
//     KEYPAD_BACK = 0x0200,
// } keypad_key_t;

typedef uint16_t keypad_key_t; // row/col code: (row<<8)|col

typedef enum {
    KEYPRESS_DOWN,
    KEYPRESS_UP,
     KEYPRESS_LONG,   // new
    KEYPRESS_REPEAT, // new (optional)
} keypad_keypress_state_t;


typedef struct{
    keypad_key_t key;
    keypad_keypress_state_t state;
} keypad_event_t;

static inline keypad_key_t keypad_make_key(int row, int col) {
    return (keypad_key_t)((row << 8) | (col & 0xFF));
}
