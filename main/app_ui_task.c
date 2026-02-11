// ===================== main/app_ui_task.c =====================
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "u8g2.h"
#include "app_state.h"
#include "esp_timer.h"
#include "ui_nav.h"
#include "ui_events.h"
#include "oled_port.h"

static const char *TAG = "app_ui";

static QueueHandle_t s_ui_q;

// Expose this to input producers (keypad/IR/BLE) via a header if you want
QueueHandle_t ui_get_queue(void) { return s_ui_q; }


// u8g2 callbacks are now provided via oled_port.h

// static u8g2_t s_u8g2;

// static void oled_init(void)
// {
//     // Example for SSD1306 128x64 I2C. Adjust constructor for your controller.
//     u8g2_Setup_ssd1306_i2c_128x64_noname_f(
//         &s_u8g2, U8G2_R0,
//         u8x8_byte_esp32_hw_i2c,
//         u8x8_gpio_and_delay_esp32
//     );

//     // Set I2C address if needed:
//     // u8g2_SetI2CAddress(&s_u8g2, 0x3C << 1);

//     u8g2_InitDisplay(&s_u8g2);
//     u8g2_SetPowerSave(&s_u8g2, 0);
//     u8g2_SetFont(&s_u8g2, u8g2_font_6x12_tf);
// }
static void ui_poll_app_events(ui_ctx_t *ctx)
{
    app_ctx_t *g = app_get_ctx();
    app_evt_t e;

    // drain quickly (non-blocking)
    while (xQueueReceive(g->evt_q, &e, 0) == pdTRUE) {
        // Convert to UI events or call a handler
        // Simplest: store into ctx->model or set flags and ctx->dirty=true

        if (e.type == APP_EVT_IR_LEARNED) {
            // Store it into app model slots with name IR_x
            xSemaphoreTake(g->model_mutex, portMAX_DELAY);
            if (g->ir_count < MAX_IR_SLOTS) {
                uint8_t idx = g->ir_count++;
                // name: IR_1..IR_32
                snprintf(g->ir_slots[idx].name, sizeof(g->ir_slots[idx].name), "IR_%u", (unsigned)(idx + 1));
                g->ir_slots[idx].code = e.ir.code;
            }
            xSemaphoreGive(g->model_mutex);
            (void)app_state_save_ir();
            ctx->ir_banner.show = true;
            ctx->ir_banner.last_code = e.ir.code;
            ctx->ir_banner.until_ms = (uint32_t)(esp_timer_get_time() / 1000ULL) + 2000; // show 2s
            // TODO: tell current screen "learn complete" (set a flag in ctx->st.dialog etc)
            // For quick: push dialog screen showing last recorded
            // (Better: have a "learn screen" that reacts)
            ctx->dirty = true;
        }

        if (e.type == APP_EVT_IR_LEARN_TIMEOUT) {
            ctx->dirty = true;
        }

        if (e.type == APP_EVT_IR_TX_DONE || e.type == APP_EVT_IR_TX_FAIL) {
            ctx->dirty = true;
        }
    }

    if (ctx->ir_banner.show) {
    uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000ULL);
    if ((int32_t)(now_ms - ctx->ir_banner.until_ms) >= 0) {
        ctx->ir_banner.show = false;
        ctx->dirty = true;
    }
}
}

static void ui_task(void *arg) {
    (void)arg;

    // ---- u8g2 init (example: SSD1306 I2C 128x64) ----
    // You must provide the low-level callbacks for ESP-IDF (I2C HAL).
    // Assume you already have u8g2 working in your project.
    u8g2_t u8;

        // Example for SSD1306 128x64 I2C. Adjust constructor for your controller.
    u8g2_Setup_ssd1306_i2c_128x64_noname_f(
        &u8, U8G2_R0,
        u8x8_byte_esp32_hw_i2c,
        u8x8_gpio_and_delay_esp32
    );

    // Set I2C address if needed:
    // u8g2_SetI2CAddress(&u8, 0x3C << 1);

    u8g2_InitDisplay(&u8);
    u8g2_SetPowerSave(&u8, 0);
    u8g2_SetFont(&u8, u8g2_font_6x12_tf);
    // u8g2_Setup_ssd1306_i2c_128x64_noname_f(&u8, U8G2_R0, your_byte_cb, your_gpio_delay_cb);
    // u8g2_InitDisplay(&u8);
    // u8g2_SetPowerSave(&u8, 0);

    ui_ctx_t ctx = {0};
    ui_nav_init(&ctx, &u8);

    // start at HOME (base screen)
    ui_nav_push(&ctx, UI_SCREEN_HOME, NULL);

    const TickType_t tick_period = pdMS_TO_TICKS(100); // UI tick (cursor blink)
    TickType_t last_wake = xTaskGetTickCount();

    while (1) {
        ui_event_t ev;
        bool got = (xQueueReceive(s_ui_q, &ev, tick_period) == pdTRUE);

        if (got) {
            ui_nav_dispatch(&ctx, &ev);
        } else {
            // periodic tick if needed
            ui_event_t tev = {.type=UI_EV_TICK, .src=UI_SRC_SYSTEM, .ts_ms=(uint32_t)(xTaskGetTickCount()*portTICK_PERIOD_MS)};
            ui_nav_dispatch(&ctx, &tev);
        }

        ui_poll_app_events(&ctx);

        ui_nav_render_if_dirty(&ctx);

        vTaskDelayUntil(&last_wake, tick_period);
    }
}



void app_ui_start(void) {
    s_ui_q = xQueueCreate(16, sizeof(ui_event_t));
    xTaskCreate(ui_task, "ui_task", 4096, NULL, 5, NULL);
}
