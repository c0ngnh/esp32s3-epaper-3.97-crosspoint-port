#pragma once

#include <cstddef>

// Prefer SPIRAM for large scratch buffers; fall back to internal heap.
void* psram_malloc(size_t size);
void psram_free(void* ptr);

void logHeapCaps(const char* tag);
