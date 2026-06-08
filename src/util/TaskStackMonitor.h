#pragma once

#include <cstddef>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

namespace TaskStackMonitor {

/** Free stack space in bytes (0 if handle is null). */
size_t freeStackBytes(TaskHandle_t handle);

/** Format e.g. "Render: 1234 B free" into buf. */
void formatStackLine(char* buf, size_t bufLen, const char* label, TaskHandle_t handle);

}  // namespace TaskStackMonitor
