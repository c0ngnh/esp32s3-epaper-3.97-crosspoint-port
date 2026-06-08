#include "PsramAlloc.h"

#include <Logging.h>

#include <esp_heap_caps.h>

void* psram_malloc(const size_t size) {
  if (size == 0) {
    return nullptr;
  }
  void* ptr = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (ptr == nullptr) {
    ptr = heap_caps_malloc(size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  }
  return ptr;
}

void psram_free(void* ptr) {
  if (ptr != nullptr) {
    heap_caps_free(ptr);
  }
}

void logHeapCaps(const char* tag) {
  const size_t freeInternal = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
  const size_t freeSpiram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
  const size_t largestInternal = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
  const size_t largestSpiram = heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM);
  LOG_INF(tag, "heap internal free=%u largest=%u | spiram free=%u largest=%u", static_cast<unsigned>(freeInternal),
          static_cast<unsigned>(largestInternal), static_cast<unsigned>(freeSpiram),
          static_cast<unsigned>(largestSpiram));
}
