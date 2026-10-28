#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

// Start the eight-node scanner; forward all 21 physical key IDs to the UI.
void start_keypad_task(QueueHandle_t ui_q);
