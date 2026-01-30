#include "esp_log.h"
#include "app_state.h"
#include "oled_ui.h"
#include "ir_nec.h"
#include "hid_ctrl.h"

static const char *TAG = "main";

static void demo_state_changes_task(void *arg)
{
    (void)arg;

    // Demo: cycle modes every 5 seconds
    for (;;) {
        app_set_mode(APP_MODE_NORMAL);
        vTaskDelay(pdMS_TO_TICKS(5000));

        app_set_mode(APP_MODE_LEARN);
        vTaskDelay(pdMS_TO_TICKS(5000));

        app_set_mode(APP_MODE_SEND);
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}

// Demo: in SEND mode, request IR send every 2 seconds using last learned code
static void demo_send_ir_task(void *arg)
{
    (void)arg;
    app_ctx_t *g = app_get_ctx();

    for (;;) {
        if (app_get_mode() == APP_MODE_SEND) {
            app_evt_t e = {.type = APP_EVT_SEND_IR};

            xSemaphoreTake(g->state_mutex, portMAX_DELAY);
            e.send_ir.code = g->last_learned;
            g->last_key = 123;     // pretend keypress
            g->counter++;
            xSemaphoreGive(g->state_mutex);

            app_post_evt(&e);
        }
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "Boot");

    // Clear, explicit init
    app_init_from_main();

    // Start subsystems/tasks
    oled_ui_start_task();
    ir_start_tasks();
    hid_start_task();

    // Demo tasks (replace with keypad task later)
    xTaskCreate(demo_state_changes_task, "demo_modes", 3072, NULL, 4, NULL);
    xTaskCreate(demo_send_ir_task, "demo_send", 3072, NULL, 4, NULL);
}
