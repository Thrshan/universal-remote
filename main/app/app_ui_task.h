#ifndef APP_UI_TASK_H
#define APP_UI_TASK_H

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Start the UI task
 */
void app_ui_start(void);

/**
 * @brief Get the UI event queue handle
 * @return Queue handle for sending UI events
 */
QueueHandle_t ui_get_queue(void);

#ifdef __cplusplus
}
#endif

#endif // APP_UI_TASK_H
