#pragma once

#include <cstddef>

namespace TxtLimits {

// Recommended for fast first-time indexing on ESP32-S3 (~30s or less typical).
constexpr size_t RECOMMENDED_MAX_BYTES = 512 * 1024;

// Above this size: confirm before opening (first index can take several minutes).
constexpr size_t WARN_BYTES = 1024 * 1024;

// Hard limit: refuse to open (RAM + index time; ~4 MB can mean 10k+ pages).
constexpr size_t HARD_MAX_BYTES = 4 * 1024 * 1024;

enum class OpenPolicy { Allow, Confirm, Refuse };

[[nodiscard]] OpenPolicy openPolicyForSize(size_t fileSizeBytes);

// Writes human-readable size into out (e.g. "512 KB", "1.2 MB").
void formatSize(char* out, size_t outLen, size_t bytes);

}  // namespace TxtLimits
