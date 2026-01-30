#include "esp_log.h"
#include "keypad.h"
#include "app_state.h"
#include "oled.h"

static const char *TAG = "main";

void app_main(void)
{
    ESP_LOGI(TAG, "Boot");
    app_init_from_main();
    start_keypad_task();
    start_oled_task();

}