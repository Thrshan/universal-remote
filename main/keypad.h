#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

// Start the eight-node keypad scanner and send navigation events to the UI queue.
void start_keypad_task(QueueHandle_t ui_q);
