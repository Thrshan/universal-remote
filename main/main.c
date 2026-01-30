#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <inttypes.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"

static const char *TAG1 = "Task1";
static const char *TAG2 = "Task2";

static int counter1 = 0;
static SemaphoreHandle_t counter_mutex;

static void task1(void *arg)
{
    (void)arg;

    for (;;) {
        xSemaphoreTake(counter_mutex, portMAX_DELAY);
        int v = ++counter1;
        xSemaphoreGive(counter_mutex);

        ESP_LOGI(TAG1, "Counter %d", v);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

static void task2(void *arg)
{
    int id = (int)(intptr_t)arg;

    for (;;) {
        xSemaphoreTake(counter_mutex, portMAX_DELAY);
        int v = ++counter1;
        xSemaphoreGive(counter_mutex);

        ESP_LOGI(TAG2, "id=%d Counter %d", id, v);
        vTaskDelay(pdMS_TO_TICKS(100 * id));
    }
}

void app_main(void)
{
    ESP_LOGI("Main", "In the main");

    counter_mutex = xSemaphoreCreateMutex();
    if (!counter_mutex) {
        ESP_LOGE("Main", "Failed to create mutex");
        return;
    }

    xTaskCreate(task1, "task1", 4096, NULL, 5, NULL);
    xTaskCreate(task2, "task2", 4096, (void *)(intptr_t)3, 5, NULL);
}
