#include "BootHealth.h"

#include <CrossPointSettings.h>
#include <CrossPointState.h>
#include <HalBoard397.h>
#include <HalStorage.h>
#include <esp_heap_caps.h>

#include "BookmarkStore.h"
#include "RecentBooksStore.h"
#include "util/PsramAlloc.h"

BootHealthReport runBootHealthCheck() {
  BootHealthReport r;
  r.sdOk = Storage.ready();

#if defined(BOARD_ESP32_S3_EPAPER_397)
  HalBoard397::DateTime dt{};
  r.rtcOk = board397.hasRtc() && board397.readRtcForDisplay(dt);
  r.spiramFree = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
  r.spiramOk = r.spiramFree > (512 * 1024);
#endif

  r.settingsOk = Storage.exists("/.crosspoint/settings.json") || Storage.exists("/.crosspoint/settings.bin");
  r.settingsDetail = r.settingsOk ? "file present" : "using defaults";

  r.stateOk = Storage.exists("/.crosspoint/state.json") || Storage.exists("/.crosspoint/state.bin");
  r.stateDetail = r.stateOk ? "file present" : "new";

  r.bookmarksOk = BOOKMARK_STORE.loadFromFile();
  r.recentOk = RECENT_BOOKS.loadFromFile();

  logHeapCaps("BOOT");
  return r;
}
