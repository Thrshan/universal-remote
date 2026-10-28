/*
 * SPDX-FileCopyrightText: 2020-2024 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: Apache-2.0
 *
 * Eight-node remote keypad scanner, ported from matrix_keyboard.
 * Each contact connects a pair of nodes; there are no fixed rows/columns.
 * Drive one node low at a time and leave all other nodes as pulled-up inputs.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "keypad.h"
#include "ui_events.h"
#include "app_state.h"

#define KEYPAD_NODE_COUNT              8
#define KEYPAD_SCAN_SETTLE_US          700
#define KEYPAD_DEBOUNCE_SAMPLE_COUNT   5
#define KEYPAD_DEBOUNCE_DELAY_MS       7
#define KEYPAD_POLL_INTERVAL_MS        10
#define KEYPAD_TASK_STACK_SIZE         4096
#define KEYPAD_TASK_PRIORITY           5

#define LONGPRESS_MS                  700
#define REPEAT_DELAY_MS               350
#define REPEAT_RATE_MS                120

static const char *TAG = "keypad";

enum {
    KEYPAD_UP = 5,
    KEYPAD_LEFT = 6,
    KEYPAD_OK = 7,
    KEYPAD_RIGHT = 8,
    KEYPAD_DOWN = 9,
    KEYPAD_BACK = 10,
};

typedef struct {
    int node_a;
    int node_b;
    int key;
} keypad_key_map_t;

typedef struct {
    int key;
    int node_a;
    int node_b;
    bool contact_detected;
    bool ignored_pair;
} keypad_scan_result_t;

// Node numbers in the contact map below are one-based, in this GPIO order.
static const gpio_num_t s_keypad_nodes[KEYPAD_NODE_COUNT] = {
    GPIO_NUM_4,
    GPIO_NUM_7,
    GPIO_NUM_15,
    GPIO_NUM_16,
    GPIO_NUM_17,
    GPIO_NUM_10,
    GPIO_NUM_11,
    GPIO_NUM_6,
};

static const keypad_key_map_t s_keypad_map[] = {
    { 2, 3, 1 },
    { 2, 5, 2 },
    { 3, 5, 3 },
    { 4, 5, 4 },
    { 2, 6, 5 },
    { 3, 6, 6 },
    { 5, 6, 7 },
    { 4, 6, 8 },
    { 2, 7, 9 },
    { 3, 7, 10 },
    { 4, 7, 11 },
    { 5, 7, 12 },
    { 2, 8, 13 },
    { 5, 8, 14 },
    { 3, 8, 15 },
    { 4, 8, 16 },
    { 1, 3, 17 },
    { 1, 5, 18 },
    { 1, 2, 19 },
    { 1, 6, 20 },
    { 1, 4, 21 },
};

static uint32_t now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000ULL);
}

static ui_event_type_t map_key_to_ui_event(int key)
{
    switch (key) {
        case KEYPAD_UP:    return UI_EV_UP;
        case KEYPAD_LEFT:  return UI_EV_LEFT;
        case KEYPAD_OK:    return UI_EV_OK;
        case KEYPAD_RIGHT: return UI_EV_RIGHT;
        case KEYPAD_DOWN:  return UI_EV_DOWN;
        case KEYPAD_BACK:  return UI_EV_BACK;
        case REMOTE_KEY_MODE: return UI_EV_MODE;
        case REMOTE_KEY_HOME: return UI_EV_HOME;
        default: return remote_key_assignable(key) ? UI_EV_KEY : UI_EV_NONE;
    }
}

static ui_event_type_t map_key_to_ui_long(int key)
{
    switch (key) {
        case KEYPAD_OK:   return UI_EV_OK_LONG;
        case KEYPAD_BACK: return UI_EV_BACK_LONG;
        case REMOTE_KEY_HOME: return UI_EV_HOME_LONG;
        case REMOTE_KEY_POWER: return UI_EV_POWER_LONG;
        case REMOTE_KEY_UP:
        case REMOTE_KEY_DOWN:
        case REMOTE_KEY_LEFT:
        case REMOTE_KEY_RIGHT:
        case REMOTE_KEY_CUSTOM4: return UI_EV_KEY_LONG;
        default:         return UI_EV_NONE;
    }
}

static bool key_allows_repeat(int key)
{
    ui_event_type_t type = map_key_to_ui_event(key);
    return type == UI_EV_UP || type == UI_EV_DOWN
        || type == UI_EV_LEFT || type == UI_EV_RIGHT
        || key == REMOTE_KEY_VOL_UP || key == REMOTE_KEY_VOL_DOWN
        || key == REMOTE_KEY_CH_UP || key == REMOTE_KEY_CH_DOWN;
}

static void emit_ui(QueueHandle_t ui_q, ui_event_type_t type, int key, bool repeat)
{
    if (type == UI_EV_NONE) {
        return;
    }

    ui_event_t event = {
        .type = type,
        .src = UI_SRC_KEYPAD,
        .ts_ms = now_ms(),
        .value = key,
        .repeat = repeat,
    };
    if (xQueueSend(ui_q, &event, 0) != pdTRUE) {
        ESP_LOGW(TAG, "UI queue full; dropped event %d", (int)type);
    }
}

static void keypad_normalize_pair(int *node_a, int *node_b)
{
    if (*node_a > *node_b) {
        int tmp = *node_a;
        *node_a = *node_b;
        *node_b = tmp;
    }
}

static void keypad_set_all_inputs_pullup(void)
{
    uint64_t pin_bit_mask = 0;
    for (int node_idx = 0; node_idx < KEYPAD_NODE_COUNT; node_idx++) {
        pin_bit_mask |= 1ULL << s_keypad_nodes[node_idx];
    }

    gpio_config_t config = {
        .pin_bit_mask = pin_bit_mask,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&config));
}

static int keypad_pair_to_key(int node_a, int node_b)
{
    keypad_normalize_pair(&node_a, &node_b);
    for (size_t map_idx = 0; map_idx < sizeof(s_keypad_map) / sizeof(s_keypad_map[0]); map_idx++) {
        if (s_keypad_map[map_idx].node_a == node_a && s_keypad_map[map_idx].node_b == node_b) {
            return s_keypad_map[map_idx].key;
        }
    }
    return 0;
}

static keypad_scan_result_t keypad_scan_raw_result(void)
{
    keypad_scan_result_t result = { 0 };

    for (int drive_idx = 0; drive_idx < KEYPAD_NODE_COUNT; drive_idx++) {
        keypad_set_all_inputs_pullup();

        // Preload the low level before enabling the output to avoid a high pulse.
        ESP_ERROR_CHECK(gpio_set_level(s_keypad_nodes[drive_idx], 0));
        ESP_ERROR_CHECK(gpio_set_direction(s_keypad_nodes[drive_idx], GPIO_MODE_OUTPUT));
        esp_rom_delay_us(KEYPAD_SCAN_SETTLE_US);

        for (int sense_idx = 0; sense_idx < KEYPAD_NODE_COUNT; sense_idx++) {
            if (sense_idx == drive_idx) {
                continue;
            }
            if (gpio_get_level(s_keypad_nodes[sense_idx]) == 0) {
                result.node_a = drive_idx + 1;
                result.node_b = sense_idx + 1;
                keypad_normalize_pair(&result.node_a, &result.node_b);
                result.contact_detected = true;
                result.ignored_pair = result.node_a == 7 && result.node_b == 8;
                result.key = keypad_pair_to_key(result.node_a, result.node_b);
                keypad_set_all_inputs_pullup();
                return result;
            }
        }
    }

    keypad_set_all_inputs_pullup();
    return result;
}

static bool keypad_scan_results_match(const keypad_scan_result_t *a, const keypad_scan_result_t *b)
{
    return a->key == b->key && a->node_a == b->node_a && a->node_b == b->node_b
        && a->contact_detected == b->contact_detected && a->ignored_pair == b->ignored_pair;
}

// Return false for an unstable scan, rather than treating bounce as a release.
static bool keypad_scan_debounced(keypad_scan_result_t *result)
{
    keypad_scan_result_t stable_result = keypad_scan_raw_result();
    TickType_t delay_ticks = pdMS_TO_TICKS(KEYPAD_DEBOUNCE_DELAY_MS);
    // The project's 100 Hz tick rounds 7 ms down to zero; always sleep a tick.
    if (delay_ticks == 0) {
        delay_ticks = 1;
    }

    for (int sample = 1; sample < KEYPAD_DEBOUNCE_SAMPLE_COUNT; sample++) {
        vTaskDelay(delay_ticks);
        keypad_scan_result_t raw_result = keypad_scan_raw_result();
        if (!keypad_scan_results_match(&raw_result, &stable_result)) {
            return false;
        }
    }

    *result = stable_result;
    return true;
}

static void keypad_task(void *arg)
{
    QueueHandle_t ui_q = (QueueHandle_t)arg;
    int last_key = 0;
    int last_unmapped_node_a = 0;
    int last_unmapped_node_b = 0;
    uint32_t press_start_ms = 0;
    uint32_t last_repeat_ms = 0;
    bool long_sent = false;

    keypad_set_all_inputs_pullup();
    ESP_LOGI(TAG, "21-key remote: 2=Mode 5/6/7/8/9=D-pad 10=Back 11=Play/Pause 12=Home");

    while (1) {
        keypad_scan_result_t current_result;
        if (keypad_scan_debounced(&current_result)) {
            int current_key = current_result.key;
            uint32_t t = now_ms();

            if (current_key != last_key) {
                if (last_key != 0) {
                    ESP_LOGI(TAG, "release event, key = %d", last_key);
                    emit_ui(ui_q, UI_EV_KEY_RELEASE, last_key, false);
                }
                if (current_key != 0) {
                    ESP_LOGI(TAG, "press event, key = %d", current_key);
                    emit_ui(ui_q, map_key_to_ui_event(current_key), current_key, false);
                }
                last_key = current_key;
                press_start_ms = t;
                last_repeat_ms = t;
                long_sent = false;
            }

            if (current_key != 0) {
                uint32_t held_ms = t - press_start_ms;
                if (!long_sent && held_ms >= LONGPRESS_MS) {
                    emit_ui(ui_q, map_key_to_ui_long(current_key), current_key, false);
                    long_sent = true;
                }
                if (key_allows_repeat(current_key) && held_ms >= REPEAT_DELAY_MS
                    && t - last_repeat_ms >= REPEAT_RATE_MS) {
                    emit_ui(ui_q, map_key_to_ui_event(current_key), current_key, true);
                    last_repeat_ms = t;
                }
            }

            if (current_result.contact_detected && current_key == 0 && !current_result.ignored_pair) {
                if (current_result.node_a != last_unmapped_node_a || current_result.node_b != last_unmapped_node_b) {
                    ESP_LOGW(TAG, "unmapped contact pair, node %d GPIO %d - node %d GPIO %d",
                             current_result.node_a, s_keypad_nodes[current_result.node_a - 1],
                             current_result.node_b, s_keypad_nodes[current_result.node_b - 1]);
                    last_unmapped_node_a = current_result.node_a;
                    last_unmapped_node_b = current_result.node_b;
                }
            } else {
                last_unmapped_node_a = 0;
                last_unmapped_node_b = 0;
            }
        }

        vTaskDelay(pdMS_TO_TICKS(KEYPAD_POLL_INTERVAL_MS));
    }
}

void start_keypad_task(QueueHandle_t ui_q)
{
    ESP_ERROR_CHECK(ui_q != NULL ? ESP_OK : ESP_ERR_INVALID_ARG);
    ESP_ERROR_CHECK(xTaskCreate(keypad_task, "keypad_task", KEYPAD_TASK_STACK_SIZE,
                               ui_q, KEYPAD_TASK_PRIORITY, NULL) == pdPASS ? ESP_OK : ESP_ERR_NO_MEM);
}
