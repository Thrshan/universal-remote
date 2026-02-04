/*
 * ESP-IDF (v5.x) simple 4x4 matrix keypad scanner for ESP32-S3
 * - Polling scan every SCAN_PERIOD_MS
 * - Active-low rows, columns pulled up
 * - Debounce + press/release events
 *
 * Wiring assumption:
 *   ROW pins = outputs (open-drain recommended), idle released/high
 *   COL pins = inputs with pull-up, read low when key pressed on active row
 *
 * Notes:
 *   - Choose GPIOs that are actually broken out on your board.
 *   - Avoid strapping / special pins, and avoid USB pins if your board uses USB.
 */

#include <stdio.h>
#include <inttypes.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "esp_err.h"
#include "driver/gpio.h"
#include "esp_rom_sys.h"   // esp_rom_delay_us()
#include "keypad.h"
#include "ui_events.h"
#include "freertos/queue.h"
#include "esp_timer.h"
// #include "app_state.h"

static const char *TAG = "keypad";

// ---- Pin map (edit to match your board wiring) ----
// static const gpio_num_t ROWS[] = { GPIO_NUM_15, GPIO_NUM_16, GPIO_NUM_18 };
// static const gpio_num_t COLS[] = { GPIO_NUM_4, GPIO_NUM_6, GPIO_NUM_7 };
static const gpio_num_t ROWS[] = { GPIO_NUM_12, GPIO_NUM_15, GPIO_NUM_16, GPIO_NUM_17 };
static const gpio_num_t COLS[] = { GPIO_NUM_4, GPIO_NUM_6, GPIO_NUM_7, GPIO_NUM_10, GPIO_NUM_11 };
#define NR_ROWS (sizeof(ROWS) / sizeof(ROWS[0]))
#define NR_COLS (sizeof(COLS) / sizeof(COLS[0]))

// ---- Scan/debounce tuning ----
#define SCAN_PERIOD_MS      10    // scan every 10 ms
#define SETTLE_US           30    // settle time after switching row
#define DEBOUNCE_SCANS      3     // require same reading for 3 consecutive scans

#define LONGPRESS_MS        700
#define REPEAT_ENABLE       1
#define REPEAT_DELAY_MS     350
#define REPEAT_RATE_MS      120

// state matrices: 1 = pressed, 0 = released
static uint8_t stable[NR_ROWS][NR_COLS];
static uint8_t candidate[NR_ROWS][NR_COLS];
static uint8_t count_same[NR_ROWS][NR_COLS];

static uint32_t press_start_ms[NR_ROWS][NR_COLS];
static uint8_t  long_sent[NR_ROWS][NR_COLS];
static uint32_t last_repeat_ms[NR_ROWS][NR_COLS];

static inline uint32_t now_ms(void) {
    return (uint32_t)(esp_timer_get_time() / 1000ULL);
}

static ui_event_type_t map_key_to_ui_event(uint16_t code)
{

//  KEYPAD_POWER    0x0003,
//  KEYPAD_UP       0x0000,
//  KEYPAD_DOWM     0x0104,
//  KEYPAD_LEFT     0x0001,
//  KEYPAD_RIGHT    0x0002,
//  KEYPAD_OK       0x0004,
//  KEYPAD_BACK     0x0101,
//  KEYPAD_HOME     0x0102,
//  KEYPAD_NUM_1    0x0100,
//  KEYPAD_NUM_2    0x0103,
//  KEYPAD_NUM_3    0x0202,
//  KEYPAD_NUM_4    0x0200,
//  KEYPAD_NUM_5    0x0204,
//  KEYPAD_NUM_6    0x0203,
//  KEYPAD_NUM_7    0x0201,
//  KEYPAD_NUM_8    0x0300,
//  KEYPAD_NUM_9    0x0303,
//  KEYPAD_NUM_0    0x0301,

    switch (code) {
        case 0x0000: return UI_EV_UP;
        case 0x0001: return UI_EV_LEFT;
        case 0x0004: return UI_EV_OK;
        case 0x0002: return UI_EV_RIGHT;
        case 0x0104: return UI_EV_DOWN;
        case 0x0101: return UI_EV_BACK;
        default:     return UI_EV_NONE;
    }
}

static ui_event_type_t map_key_to_ui_long(uint16_t code)
{
    switch (code) {
        case 0x0101: return UI_EV_OK_LONG;    // OK long
        case 0x0200: return UI_EV_BACK_LONG;  // BACK long
        default:     return UI_EV_NONE;
    }
}

static bool key_allows_repeat(uint16_t code)
{
    ui_event_type_t t = map_key_to_ui_event(code);
    return (t == UI_EV_UP || t == UI_EV_DOWN || t == UI_EV_LEFT || t == UI_EV_RIGHT);
}

static void emit_ui(QueueHandle_t ui_q, ui_event_type_t type)
{
    if (!ui_q || type == UI_EV_NONE) return;
    ui_event_t ev = {
        .type = type,
        .src  = UI_SRC_KEYPAD,
        .ts_ms = now_ms(),
        .value = 0
    };
    xQueueSend(ui_q, &ev, 0);
}


// helper: configure rows/cols
static void keypad_gpio_init(void)
{
    // Configure ROW pins: open-drain output, default released/high
    for (int r = 0; r < (int)NR_ROWS; r++) {
        gpio_config_t io = {
            .pin_bit_mask = 1ULL << ROWS[r],
            .mode = GPIO_MODE_OUTPUT_OD,       // open-drain output
            .pull_up_en = GPIO_PULLUP_ENABLE,  // helps keep it high when released
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE
        };
        ESP_ERROR_CHECK(gpio_config(&io));
        gpio_set_level(ROWS[r], 1); // released (Hi-Z), pulled up
    }

    // Configure COL pins: input with pull-up
    for (int c = 0; c < (int)NR_COLS; c++) {
        gpio_config_t io = {
            .pin_bit_mask = 1ULL << COLS[c],
            .mode = GPIO_MODE_INPUT,
            .pull_up_en = GPIO_PULLUP_ENABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE
        };
        ESP_ERROR_CHECK(gpio_config(&io));
    }
}

// Read current raw pressed state for one row (pressed=1, released=0)
static void read_row_raw(int row, uint8_t out_cols[NR_COLS])
{
    // Release all rows first (prevents ghost effects while switching)
    for (int r = 0; r < (int)NR_ROWS; r++) {
        gpio_set_level(ROWS[r], 1); // release
    }

    // Activate this row (active-low)
    gpio_set_level(ROWS[row], 0);

    // Let signals settle
    esp_rom_delay_us(SETTLE_US);

    // Read all columns
    for (int c = 0; c < (int)NR_COLS; c++) {
        int level = gpio_get_level(COLS[c]);   // 1 = pulled up, 0 = pulled down by key press
        out_cols[c] = (level == 0) ? 1 : 0;    // pressed=1 if column reads low
    }

    // Deactivate row
    gpio_set_level(ROWS[row], 1);
}

static inline uint16_t make_key_code(int row, int col)
{
    // same style as many examples: upper byte=row, lower byte=col
    return (uint16_t)((row << 8) | (col & 0xFF));
}

static void print_event(const char *what, int row, int col)
{
    uint16_t code = make_key_code(row, col);
    ESP_LOGI(TAG, "%s: row=%d col=%d code=0x%04" PRIx16, what, row, col, code);
}

void keypad_task(void *arg)
{
    QueueHandle_t ui_q = (QueueHandle_t)arg;

    esp_log_level_set("*", ESP_LOG_INFO);
    ESP_LOGI(TAG, "matrix keypad polling scanner starting");

    keypad_gpio_init();

    memset(stable, 0, sizeof(stable));
    memset(candidate, 0, sizeof(candidate));
    memset(count_same, 0, sizeof(count_same));
    memset(press_start_ms, 0, sizeof(press_start_ms));
    memset(long_sent, 0, sizeof(long_sent));
    memset(last_repeat_ms, 0, sizeof(last_repeat_ms));

    uint8_t raw_cols[NR_COLS];

    while (1) {
        uint32_t t = now_ms();

        for (int r = 0; r < (int)NR_ROWS; r++) {
            read_row_raw(r, raw_cols);

            for (int c = 0; c < (int)NR_COLS; c++) {
                uint8_t raw = raw_cols[c];

                if (raw == candidate[r][c]) {
                    if (count_same[r][c] < 255) count_same[r][c]++;
                } else {
                    candidate[r][c] = raw;
                    count_same[r][c] = 1;
                }

                if (count_same[r][c] >= DEBOUNCE_SCANS && stable[r][c] != candidate[r][c]) {
                    stable[r][c] = candidate[r][c];

                    uint16_t code = make_key_code(r, c);

                    if (stable[r][c]) {
                        // DOWN
                        press_start_ms[r][c] = t;
                        long_sent[r][c] = 0;
                        last_repeat_ms[r][c] = t;

                        print_event("DOWN", r, c);

                        ui_event_type_t ev = map_key_to_ui_event(code);
                        emit_ui(ui_q, ev);

                    } else {
                        // UP
                        print_event("UP", r, c);
                        press_start_ms[r][c] = 0;
                        long_sent[r][c] = 0;
                        last_repeat_ms[r][c] = 0;
                    }
                }
            }
        }

        // Long press + repeat pass (based on debounced stable[][])
        for (int r = 0; r < (int)NR_ROWS; r++) {
            for (int c = 0; c < (int)NR_COLS; c++) {
                if (!stable[r][c]) continue; // not pressed

                uint16_t code = make_key_code(r, c);

                // long press
                if (!long_sent[r][c] && press_start_ms[r][c] != 0) {
                    if ((t - press_start_ms[r][c]) >= LONGPRESS_MS) {
                        ui_event_type_t lev = map_key_to_ui_long(code);
                        if (lev != UI_EV_NONE) {
                            emit_ui(ui_q, lev);
                            long_sent[r][c] = 1;
                            ESP_LOGI(TAG, "LONG: code=0x%04x", code);
                        }
                    }
                }

#if REPEAT_ENABLE
                // repeat
                if (key_allows_repeat(code) && press_start_ms[r][c] != 0) {
                    uint32_t held = t - press_start_ms[r][c];
                    if (held >= REPEAT_DELAY_MS) {
                        if ((t - last_repeat_ms[r][c]) >= REPEAT_RATE_MS) {
                            emit_ui(ui_q, map_key_to_ui_event(code));
                            last_repeat_ms[r][c] = t;
                        }
                    }
                }
#endif
            }
        }

        vTaskDelay(pdMS_TO_TICKS(SCAN_PERIOD_MS));
    }
}


void start_keypad_task(void *ui_q)
{
        xTaskCreate(keypad_task, "keypad_task", 4096, ui_q, 5, NULL);
}
