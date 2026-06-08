#include "TaskStackMonitor.h"

#include <cstdio>

namespace TaskStackMonitor {

size_t freeStackBytes(const TaskHandle_t handle) {
  if (handle == nullptr) {
    return 0;
  }
  return static_cast<size_t>(uxTaskGetStackHighWaterMark(handle)) * sizeof(StackType_t);
}

void formatStackLine(char* buf, const size_t bufLen, const char* label, const TaskHandle_t handle) {
  if (buf == nullptr || bufLen == 0) {
    return;
  }
  snprintf(buf, bufLen, "%s: %u B free", label, static_cast<unsigned>(freeStackBytes(handle)));
}

}  // namespace TaskStackMonitor
