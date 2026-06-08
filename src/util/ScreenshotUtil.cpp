#include "ScreenshotUtil.h"

#include <Arduino.h>
#include <BitmapHelpers.h>
#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalDisplay.h>
#include <HalGPIO.h>
#include <HalStorage.h>
#include <Logging.h>

#include <cstring>
#include <string>

#include <I18n.h>

#include "Bitmap.h"  // Required for BmpHeader struct definition
#include "activities/Activity.h"
#include "activities/ActivityManager.h"
#include "activities/reader/ReaderUtils.h"
#include "components/UITheme.h"

void ScreenshotUtil::buildFilename(const ScreenshotInfo& info, char* buf, size_t bufSize) {
  const unsigned long ts = millis();

  if (info.readerType == ScreenshotInfo::ReaderType::None || info.title[0] == '\0') {
    snprintf(buf, bufSize, "/screenshots/screenshot-%lu.bmp", ts);
    return;
  }

  char sanitizedTitle[64];
  FsHelpers::sanitizePathComponentForFat32(info.title, sanitizedTitle, sizeof(sanitizedTitle));
  if (sanitizedTitle[0] == '\0') {
    snprintf(buf, bufSize, "/screenshots/screenshot-%lu.bmp", ts);
    return;
  }

  int pct = info.progressPercent;
  if (pct < 0) pct = 0;
  if (pct > 100) pct = 100;

  // Display spine index as 1-based for user-facing filenames
  const int chapterNum = info.spineIndex + 1;

  if (info.readerType == ScreenshotInfo::ReaderType::Epub && info.spineIndex >= 0) {
    snprintf(buf, bufSize, "/screenshots/%s/%s_ch%d_p%d_%dpct_%lu.bmp", sanitizedTitle, sanitizedTitle, chapterNum,
             info.currentPage, pct, ts);
  } else {
    snprintf(buf, bufSize, "/screenshots/%s/%s_p%d_%dpct_%lu.bmp", sanitizedTitle, sanitizedTitle, info.currentPage,
             pct, ts);
  }

  // Truncate title if total path exceeds FAT32 limit
  if (strlen(buf) > 255) {
    size_t titleLen = strlen(sanitizedTitle);
    size_t overhead = strlen(buf) - 2 * titleLen;
    if (overhead < 255) {
      size_t maxTitleLen = (255 - overhead) / 2;
      // Walk back to a valid UTF-8 boundary to avoid corrupting multibyte characters
      while (maxTitleLen > 0 && (sanitizedTitle[maxTitleLen] & 0xC0) == 0x80) {
        maxTitleLen--;
      }
      sanitizedTitle[maxTitleLen] = '\0';
      if (info.readerType == ScreenshotInfo::ReaderType::Epub && info.spineIndex >= 0) {
        snprintf(buf, bufSize, "/screenshots/%s/%s_ch%d_p%d_%dpct_%lu.bmp", sanitizedTitle, sanitizedTitle, chapterNum,
                 info.currentPage, pct, ts);
      } else {
        snprintf(buf, bufSize, "/screenshots/%s/%s_p%d_%dpct_%lu.bmp", sanitizedTitle, sanitizedTitle, info.currentPage,
                 pct, ts);
      }
    } else {
      snprintf(buf, bufSize, "/screenshots/screenshot-%lu.bmp", ts);
    }
  }
}

void ScreenshotUtil::takeScreenshot(GfxRenderer& renderer) {
  const uint8_t* fb = renderer.getFrameBuffer();
  if (!fb) {
    LOG_ERR("SCR", "Framebuffer not available");
    return;
  }

  if (!Storage.exists("/screenshots")) {
    if (!Storage.mkdir("/screenshots")) {
      LOG_ERR("SCR", "Failed to create /screenshots on SD card");
      return;
    }
  }

  ScreenshotInfo info = activityManager.getScreenshotInfo();
  char filename[256];
  buildFilename(info, filename, sizeof(filename));

  bool saved = saveFramebufferAsBmp(filename, fb, renderer.getDisplayWidth(), renderer.getDisplayHeight());
  if (saved) {
    LOG_INF("SCR", "Screenshot saved to %s", filename);
  } else {
    LOG_ERR("SCR", "Failed to save screenshot to %s", filename);
    return;
  }

  if (!renderer.storeBwBuffer()) {
    LOG_INF("SCR", "Popup skipped (BW backup alloc failed); file saved");
#if defined(BOARD_ESP32_S3_EPAPER_397)
    if (activityManager.isReaderActivity()) {
      ReaderUtils::requestFullRefreshOnNextPage();
    } else {
      display.requestResync();
      renderer.displayBuffer(HalDisplay::FULL_REFRESH);
    }
#endif
    activityManager.requestUpdate(true);
    return;
  }

  GUI.drawPopup(renderer, tr(STR_SCREENSHOT_SAVED));
  delay(300);
  renderer.restoreBwBuffer();
#if defined(BOARD_ESP32_S3_EPAPER_397)
  gpio.suppressConfirmAfterGesture();
  if (activityManager.isReaderActivity()) {
    // Keep popup on panel briefly; next page turn does FULL_REFRESH (clears SSD1677 ghost).
    ReaderUtils::requestFullRefreshOnNextPage();
  } else {
    display.requestResync();
    renderer.displayBuffer(HalDisplay::FULL_REFRESH);
  }
#else
  renderer.displayBuffer();
#endif
  activityManager.requestUpdate(true);
}

bool ScreenshotUtil::saveFramebufferAsBmp(const char* filename, const uint8_t* framebuffer, int width, int height) {
  if (!framebuffer) {
    return false;
  }

  // Note: the width and height, we rotate the image 90d counter-clockwise to match the default display orientation
  int phyWidth = height;
  int phyHeight = width;

  const uint32_t rowSizePadded = static_cast<uint32_t>((phyWidth + 31) / 32 * 4);
  // Portrait 397 is 480x800 logical -> 800px-wide BMP rows (104 bytes). X3 tops out at 68 bytes.
  constexpr size_t kMaxRowSize = (HalDisplay::DISPLAY_WIDTH + 31) / 32 * 4;
  if (rowSizePadded > kMaxRowSize) {
    LOG_ERR("SCR", "Row size %u exceeds buffer capacity %u", rowSizePadded, static_cast<unsigned>(kMaxRowSize));
    return false;
  }

  std::string path(filename);
  size_t last_slash = path.find_last_of('/');
  if (last_slash != std::string::npos) {
    const std::string dir = path.substr(0, last_slash);
    if (!Storage.exists("/screenshots")) {
      if (!Storage.mkdir("/screenshots")) {
        LOG_ERR("SCR", "Failed to create /screenshots");
        return false;
      }
    }
    if (!Storage.exists(dir.c_str())) {
      if (!Storage.mkdir(dir.c_str())) {
        LOG_ERR("SCR", "Failed to create directory %s", dir.c_str());
        return false;
      }
    }
  }

  FsFile file;
  if (!Storage.openFileForWrite("SCR", filename, file)) {
    LOG_ERR("SCR", "Failed to open %s for write", filename);
    return false;
  }

  BmpHeader header;

  createBmpHeader(&header, phyWidth, phyHeight, BmpRowOrder::BottomUp);

  bool write_error = false;
  if (file.write(reinterpret_cast<uint8_t*>(&header), sizeof(header)) != sizeof(header)) {
    write_error = true;
  }

  if (write_error) {
    file.close();
    Storage.remove(filename);
    return false;
  }

  // Rotate 90d counter-clockwise on-the-fly while writing to save memory
  uint8_t rowBuffer[kMaxRowSize];
  memset(rowBuffer, 0, rowSizePadded);

  for (int outY = 0; outY < phyHeight; outY++) {
    for (int outX = 0; outX < phyWidth; outX++) {
      // 90d counter-clockwise: source (srcX, srcY)
      // BMP rows are bottom-to-top, so outY=0 is the bottom of the displayed image
      int srcX = width - 1 - outY;     // phyHeight == width
      int srcY = phyWidth - 1 - outX;  // phyWidth == height
      int fbIndex = srcY * (width / 8) + (srcX / 8);
      uint8_t pixel = (framebuffer[fbIndex] >> (7 - (srcX % 8))) & 0x01;
      rowBuffer[outX / 8] |= pixel << (7 - (outX % 8));
    }
    if (file.write(rowBuffer, rowSizePadded) != rowSizePadded) {
      write_error = true;
      break;
    }
    memset(rowBuffer, 0, rowSizePadded);  // Clear the buffer for the next row
  }

  // Explicitly close() file before calling Storage.remove()
  file.close();

  if (write_error) {
    Storage.remove(filename);
    return false;
  }

  return true;
}
