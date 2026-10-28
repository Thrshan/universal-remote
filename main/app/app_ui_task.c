#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_timer.h"
#include "u8g2.h"
#include "app_state.h"
#include "ui_nav.h"
#include "ui_events.h"
#include "oled_port.h"
#include "app_ui_task.h"

static QueueHandle_t s_ui_q;
QueueHandle_t ui_get_queue(void) { return s_ui_q; }

static void ui_task(void *arg)
{
    (void)arg;
    static u8g2_t display;
    static ui_ctx_t ctx;
    // Rotate the physical 128x64 panel clockwise: logical canvas is 64x128.
    oled_setup_portrait(&display,
        u8x8_byte_esp32_hw_i2c, u8x8_gpio_and_delay_esp32);
    u8g2_InitDisplay(&display);
    vTaskDelay(pdMS_TO_TICKS(100));
    ui_nav_init(&ctx, &display);
    ui_nav_push(&ctx, UI_SCREEN_SPLASH, NULL);
    ui_nav_render_if_dirty(&ctx);
    // Fill display RAM before switching the panel on to avoid a garbage frame.
    u8g2_SetPowerSave(&display, 0);

    for (;;) {
        ui_event_t event;
        if (xQueueReceive(s_ui_q, &event, pdMS_TO_TICKS(25)) == pdTRUE)
            ui_nav_dispatch(&ctx, &event);
        app_evt_t app_event;
        while (xQueueReceive(app_get_ctx()->evt_q, &app_event, 0) == pdTRUE)
            ui_handle_app_event(&ctx, &app_event);
        ui_event_t tick = {
            .type = UI_EV_TICK, .src = UI_SRC_SYSTEM,
            .ts_ms = (uint32_t)(esp_timer_get_time() / 1000ULL),
        };
        ui_nav_dispatch(&ctx, &tick);
        ui_nav_render_if_dirty(&ctx);
    }
}
void app_ui_start(void)
{
    s_ui_q = xQueueCreate(32, sizeof(ui_event_t));
    ESP_ERROR_CHECK(s_ui_q ? ESP_OK : ESP_ERR_NO_MEM);
    ESP_ERROR_CHECK(xTaskCreate(ui_task, "ui_task", 8192, NULL, 5, NULL) == pdPASS
        ? ESP_OK : ESP_ERR_NO_MEM);
}
