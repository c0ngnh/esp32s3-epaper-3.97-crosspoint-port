#include "TxtLimits.h"

#include <cstdio>

namespace TxtLimits {

OpenPolicy openPolicyForSize(const size_t fileSizeBytes) {
  if (fileSizeBytes > HARD_MAX_BYTES) {
    return OpenPolicy::Refuse;
  }
  if (fileSizeBytes > WARN_BYTES) {
    return OpenPolicy::Confirm;
  }
  return OpenPolicy::Allow;
}

void formatSize(char* out, const size_t outLen, const size_t bytes) {
  if (!out || outLen == 0) {
    return;
  }
  if (bytes >= 1024 * 1024) {
    const float mb = static_cast<float>(bytes) / (1024.0f * 1024.0f);
    snprintf(out, outLen, "%.1f MB", mb);
  } else if (bytes >= 1024) {
    const unsigned kb = static_cast<unsigned>((bytes + 512) / 1024);
    snprintf(out, outLen, "%u KB", kb);
  } else {
    snprintf(out, outLen, "%u B", static_cast<unsigned>(bytes));
  }
}

}  // namespace TxtLimits
