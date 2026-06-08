#include "SdMediaFolders.h"

#if defined(BOARD_ESP32_S3_EPAPER_397)

#include <HalStorage.h>
#include <Logging.h>

void ensureSdMediaFolders() {
  static constexpr const char* kDirs[] = {"/pictures", "/music", "/recordings"};
  for (const char* dir : kDirs) {
    if (Storage.exists(dir)) {
      continue;
    }
    if (Storage.mkdir(dir)) {
      LOG_INF("SD", "Created folder %s", dir);
    } else {
      LOG_ERR("SD", "Failed to create folder %s", dir);
    }
  }
}

#endif
