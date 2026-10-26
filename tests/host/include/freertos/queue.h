#pragma once
#include "FreeRTOS.h"
typedef struct host_queue *QueueHandle_t;
QueueHandle_t xQueueCreate(unsigned count, unsigned size);
int xQueueSend(QueueHandle_t queue, const void *item, TickType_t timeout);
int xQueueReceive(QueueHandle_t queue, void *item, TickType_t timeout);
