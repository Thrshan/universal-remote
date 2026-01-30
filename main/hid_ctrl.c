#include "hid_ctrl.h"
#include "app_state.h"
#include "esp_log.h"

static const char *TAG = "hid";

// IMPORTANT:
// ESP-IDF has multiple HID examples (Bluedroid vs NimBLE). The function names differ.
// Put your example’s init + send-report calls inside these two wrappers.
static void hid_init_from_example(void)
{
    // Paste/init from:
    // "ESP HID device example code for bluetooth"
    // (your current working init sequence goes here)
}

static void hid_send_volume_up(void)
{
    // Paste the “volume up” consumer control report send from your example.
}

static void hid_send_volume_down(void)
{
    // Paste the “volume down” consumer control report send from your example.
}

static void hid_task(void *arg)
{
    (void)arg;
    app_ctx_t *g = app_get_ctx();

    hid_init_from_example();
    ESP_LOGI(TAG, "HID init done");

    bool up = true;

    for (;;) {
        // Only do auto volume changes when BT enabled (or when in a specific mode)
        EventBits_t b = xEventGroupGetBits(g->flags);
        if ((b & EVBIT_BT_ENABLED) == 0) {
            vTaskDelay(pdMS_TO_TICKS(200));
            continue;
        }

        // Example: auto volume tick every 1 second
        if (up) hid_send_volume_up();
        else    hid_send_volume_down();
        up = !up;

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

void hid_start_task(void)
{
    xTaskCreate(hid_task, "hid_task", 8192, NULL, 6, NULL);
}
