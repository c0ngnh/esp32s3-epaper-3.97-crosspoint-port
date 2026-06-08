#include "ClearCacheActivity.h"

#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>

#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/AppScreenLayout.h"

void ClearCacheActivity::onEnter() {
  Activity::onEnter();

  state = WARNING;
  requestUpdate();
}

void ClearCacheActivity::onExit() { Activity::onExit(); }

void ClearCacheActivity::render(RenderLock&&) {
  renderer.clearScreen();
  const auto screen = AppScreenLayout::listScreen(renderer);

  GUI.drawHeader(renderer, screen.header, tr(STR_CLEAR_READING_CACHE));

  if (state == WARNING) {
    const int lineH = renderer.getLineHeight(UI_10_FONT_ID);
    int y = screen.body.y + screen.body.height / 2 - lineH * 2;
    renderer.drawCenteredText(UI_10_FONT_ID, y, tr(STR_CLEAR_CACHE_WARNING_1), true);
    y += lineH;
    renderer.drawCenteredText(UI_10_FONT_ID, y, tr(STR_CLEAR_CACHE_WARNING_2), true, EpdFontFamily::BOLD);
    y += lineH;
    renderer.drawCenteredText(UI_10_FONT_ID, y, tr(STR_CLEAR_CACHE_WARNING_3), true);
    y += lineH;
    renderer.drawCenteredText(UI_10_FONT_ID, y, tr(STR_CLEAR_CACHE_WARNING_4), true);

    const auto labels = mappedInput.mapLabels(tr(STR_CANCEL), tr(STR_CLEAR_BUTTON), "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer();
    return;
  }

  if (state == CLEARING) {
    AppScreenLayout::drawBodyMessage(renderer, screen.body, tr(STR_CLEARING_CACHE));
    renderer.displayBuffer();
    return;
  }

  if (state == SUCCESS) {
    AppScreenLayout::drawBodyMessage(renderer, screen.body, tr(STR_CACHE_CLEARED), true);
    std::string resultText = std::to_string(clearedCount) + " " + std::string(tr(STR_ITEMS_REMOVED));
    if (failedCount > 0) {
      resultText += ", " + std::to_string(failedCount) + " " + std::string(tr(STR_FAILED_LOWER));
    }
    const int detailY = screen.body.y + screen.body.height / 2 + renderer.getLineHeight(UI_10_FONT_ID);
    renderer.drawCenteredText(UI_10_FONT_ID, detailY, resultText.c_str());

    const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer();
    return;
  }

  if (state == FAILED) {
    AppScreenLayout::drawBodyMessage(renderer, screen.body, tr(STR_CLEAR_CACHE_FAILED), true);
    const int detailY = screen.body.y + screen.body.height / 2 + renderer.getLineHeight(UI_10_FONT_ID);
    renderer.drawCenteredText(UI_10_FONT_ID, detailY, tr(STR_CHECK_SERIAL_OUTPUT));

    const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer();
    return;
  }
}

void ClearCacheActivity::clearCache() {
  LOG_DBG("CLEAR_CACHE", "Clearing cache...");

  // Open .crosspoint directory
  auto root = Storage.open("/.crosspoint");
  if (!root || !root.isDirectory()) {
    LOG_DBG("CLEAR_CACHE", "Failed to open cache directory");
    if (root) root.close();
    state = FAILED;
    requestUpdate();
    return;
  }

  clearedCount = 0;
  failedCount = 0;
  char name[128];

  // Iterate through all entries in the directory
  for (auto file = root.openNextFile(); file; file = root.openNextFile()) {
    file.getName(name, sizeof(name));
    String itemName(name);

    // Only delete directories starting with epub_ or xtc_
    if (file.isDirectory() && (itemName.startsWith("epub_") || itemName.startsWith("xtc_"))) {
      String fullPath = "/.crosspoint/" + itemName;
      LOG_DBG("CLEAR_CACHE", "Removing cache: %s", fullPath.c_str());

      file.close();  // Close before attempting to delete

      if (Storage.removeDir(fullPath.c_str())) {
        clearedCount++;
      } else {
        LOG_ERR("CLEAR_CACHE", "Failed to remove: %s", fullPath.c_str());
        failedCount++;
      }
    } else {
      file.close();
    }
  }
  root.close();

  LOG_DBG("CLEAR_CACHE", "Cache cleared: %d removed, %d failed", clearedCount, failedCount);

  state = SUCCESS;
  requestUpdate();
}

void ClearCacheActivity::loop() {
  if (state == WARNING) {
#if defined(BOARD_ESP32_S3_EPAPER_397)
    if (consumeConfirmClick()) {
#else
    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
#endif
      LOG_DBG("CLEAR_CACHE", "User confirmed, starting cache clear");
      {
        RenderLock lock(*this);
        state = CLEARING;
      }
      requestUpdateAndWait();

      clearCache();
    }

    if (mappedInput.wasBackClicked()) {
      LOG_DBG("CLEAR_CACHE", "User cancelled");
      goBack();
    }
    return;
  }

  if (state == SUCCESS || state == FAILED) {
    if (mappedInput.wasBackClicked()) {
      goBack();
    }
    return;
  }
}
