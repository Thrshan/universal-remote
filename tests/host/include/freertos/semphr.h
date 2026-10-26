#pragma once
#include "FreeRTOS.h"
typedef void *SemaphoreHandle_t;
static inline SemaphoreHandle_t xSemaphoreCreateMutex(void) { return (void *)1; }
static inline int xSemaphoreTake(SemaphoreHandle_t mutex, TickType_t timeout)
{ (void)mutex; (void)timeout; return 1; }
static inline int xSemaphoreGive(SemaphoreHandle_t mutex) { (void)mutex; return 1; }
