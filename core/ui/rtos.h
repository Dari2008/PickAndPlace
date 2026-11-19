// FreeRTOS mutex calls used by the ESP32 UI code, on std::mutex
#pragma once

#include <mutex>

typedef std::mutex *SemaphoreHandle_t;
#define portMAX_DELAY 0
inline SemaphoreHandle_t xSemaphoreCreateMutex() { return new std::mutex; }
inline void xSemaphoreTake(SemaphoreHandle_t m, int) { m->lock(); }
inline void xSemaphoreGive(SemaphoreHandle_t m) { m->unlock(); }
