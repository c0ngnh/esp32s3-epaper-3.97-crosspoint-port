#include "KOReaderSyncActivity.h"

#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>
#include <WiFi.h>
#include <esp_sntp.h>

#include <algorithm>
#include <cassert>

#include "Epub/Section.h"
#include "EpubReaderUtils.h"
#include "KOReaderCredentialStore.h"
#include "KOReaderDocumentId.h"
#include "MappedInputManager.h"
#include "ReaderUtils.h"
#include "activities/ActivityManager.h"
#include "activities/network/WifiSelectionActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/AppScreenLayout.h"

namespace {
void syncTimeWithNTP() {
  // Stop SNTP if already running (can't reconfigure while running)
  if (esp_sntp_enabled()) {
    esp_sntp_stop();
  }

  // Configure SNTP
  esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);
  esp_sntp_setservername(0, "pool.ntp.org");
  esp_sntp_init();

  // Wait for time to sync (with timeout)
  int retry = 0;
  const int maxRetries = 50;  // 5 seconds max
  while (sntp_get_sync_status() != SNTP_SYNC_STATUS_COMPLETED && retry < maxRetries) {
    vTaskDelay(100 / portTICK_PERIOD_MS);
    retry++;
  }

  if (retry < maxRetries) {
    LOG_DBG("KOSync", "NTP time synced");
  } else {
    LOG_DBG("KOSync", "NTP sync timeout, using fallback");
  }
}
void wifiOff() {
  if (esp_sntp_enabled()) {
    esp_sntp_stop();
  }
  WiFi.disconnect(false);
  delay(100);
  WiFi.mode(WIFI_OFF);
  delay(100);
}
}  // namespace

void KOReaderSyncActivity::ensureEpubLoaded() {
  if (!epub) {
    LOG_DBG("KOSync", "Loading epub for progress mapping (heap: %u)", (unsigned)ESP.getFreeHeap());
    epub = std::make_shared<Epub>(epubPath, "/.crosspoint");
    epub->setupCacheDir();
    // Load metadata only (no CSS needed for progress mapping, don't rebuild if cache is missing).
    if (!epub->load(false, true)) {
      LOG_ERR("KOSync", "Failed to load epub for progress mapping");
      epub.reset();
      return;
    }
    LOG_DBG("KOSync", "Epub loaded (heap: %u)", (unsigned)ESP.getFreeHeap());
  }
}

void KOReaderSyncActivity::saveProgressAndReturn(int spineIndex, int page) {
  // epub is guaranteed non-null here: ensureEpubLoaded() was called in performSync() before
  // SHOWING_RESULT state is entered, and this method is only called from that state.
  assert(epub);
  if (!EpubReaderUtils::saveProgress(*epub, spineIndex, page, 0)) {
    {
      RenderLock lock(*this);
      state = SYNC_FAILED;
      statusMessage = tr(STR_SAVE_PROGRESS_FAILED);
    }
    requestUpdate(true);
    return;
  }
  returnToReader();
}

void KOReaderSyncActivity::returnToReader() { activityManager.goToReader(epubPath); }

void KOReaderSyncActivity::onWifiSelectionComplete(const bool success) {
  if (!success) {
    LOG_DBG("KOSync", "WiFi connection failed, exiting");
    returnToReader();
    return;
  }

  LOG_DBG("KOSync", "WiFi connected, starting sync");

  {
    RenderLock lock(*this);
    state = SYNCING;
    statusMessage = tr(STR_SYNCING_TIME);
  }
  requestUpdate(true);

  // Sync time with NTP before making API requests
  syncTimeWithNTP();

  {
    RenderLock lock(*this);
    statusMessage = tr(STR_CALC_HASH);
  }
  requestUpdate(true);

  performSync();
}

void KOReaderSyncActivity::performSync() {
  // Calculate document hash based on user's preferred method
  if (KOREADER_STORE.getMatchMethod() == DocumentMatchMethod::FILENAME) {
    documentHash = KOReaderDocumentId::calculateFromFilename(epubPath);
  } else {
    documentHash = KOReaderDocumentId::calculate(epubPath);
  }
  if (documentHash.empty()) {
    {
      RenderLock lock(*this);
      state = SYNC_FAILED;
      statusMessage = tr(STR_HASH_FAILED);
    }
    requestUpdate(true);
    return;
  }

  LOG_DBG("KOSync", "Document hash: %s", documentHash.c_str());

  {
    RenderLock lock(*this);
    statusMessage = tr(STR_FETCH_PROGRESS);
  }
  requestUpdateAndWait();

  // Fetch remote progress
  const auto result = KOReaderSyncClient::getProgress(documentHash, remoteProgress);

  if (result == KOReaderSyncClient::NOT_FOUND) {
    // No remote progress - offer to upload
    {
      RenderLock lock(*this);
      state = NO_REMOTE_PROGRESS;
      hasRemoteProgress = false;
    }
    requestUpdate(true);
    return;
  }

  if (result != KOReaderSyncClient::OK) {
    {
      RenderLock lock(*this);
      state = SYNC_FAILED;
      statusMessage = KOReaderSyncClient::errorString(result);
    }
    requestUpdate(true);
    return;
  }

  // Epub was released before sync to free RAM for the TLS handshake — reload it now.
  hasRemoteProgress = true;
  ensureEpubLoaded();
  if (!epub) {
    {
      RenderLock lock(*this);
      state = SYNC_FAILED;
      statusMessage = tr(STR_SYNC_FAILED_MSG);
    }
    requestUpdate(true);
    return;
  }

  KOReaderPosition koPos = {remoteProgress.progress, remoteProgress.percentage};
  remotePosition = ProgressMapper::toCrossPoint(epub, koPos, currentSpineIndex, totalPagesInSpine);

  // Refine page using section cache LUTs: li index, anchor, or paragraph index.
  if (remotePosition.hasLiIndex || remotePosition.xpathAnchorId[0] != '\0' || remotePosition.hasParagraphIndex) {
    Section tempSection(epub, remotePosition.spineIndex, renderer);
    bool refined = false;
    if (remotePosition.hasLiIndex) {
      const auto liPage = tempSection.getPageForListItemIndex(remotePosition.liIndex);
      if (liPage.has_value()) {
        LOG_DBG("KOSync", "Li index %u -> page %d (was %d)", remotePosition.liIndex, *liPage,
                remotePosition.pageNumber);
        remotePosition.pageNumber = *liPage;
        refined = true;
      } else {
        LOG_DBG("KOSync", "Li index %u not found in section LUT", remotePosition.liIndex);
      }
    }
    if (!refined && remotePosition.xpathAnchorId[0] != '\0') {
      const auto anchorPage = tempSection.getPageForAnchor(std::string(remotePosition.xpathAnchorId));
      if (anchorPage.has_value()) {
        LOG_DBG("KOSync", "Anchor '%s' -> page %d (was %d)", remotePosition.xpathAnchorId, *anchorPage,
                remotePosition.pageNumber);
        remotePosition.pageNumber = *anchorPage;
        refined = true;
      } else {
        LOG_DBG("KOSync", "Anchor '%s' not found in section cache", remotePosition.xpathAnchorId);
      }
    }
    if (!refined && remotePosition.hasParagraphIndex) {
      const auto paragraphPage = tempSection.getPageForParagraphIndex(remotePosition.paragraphIndex);
      const auto nextParagraphPage = tempSection.getPageForParagraphIndex(remotePosition.paragraphIndex + 1);
      if (paragraphPage.has_value()) {
        int refinedPage = std::max(remotePosition.pageNumber, static_cast<int>(*paragraphPage));
        if (nextParagraphPage.has_value()) {
          const int lutSpan = static_cast<int>(*nextParagraphPage) - static_cast<int>(*paragraphPage);
          // Only cap when the LUT span is >1. A span of 1 means the LUT granularity is too
          // coarse to trust over the intra-spine position (e.g. a stale cache where the paragraph
          // occupies different pages than at build time).
          if (lutSpan > 1 && refinedPage >= static_cast<int>(*nextParagraphPage)) {
            refinedPage = static_cast<int>(*nextParagraphPage) - 1;
          }
        }
        char nextParaBuf[8];
        if (nextParagraphPage.has_value())
          snprintf(nextParaBuf, sizeof(nextParaBuf), "%d", *nextParagraphPage);
        else
          snprintf(nextParaBuf, sizeof(nextParaBuf), "none");
        LOG_DBG("KOSync", "Paragraph %u -> LUT page %d, nextPara page %s, intra page %d, using %d",
                remotePosition.paragraphIndex, *paragraphPage, nextParaBuf, remotePosition.pageNumber, refinedPage);
        remotePosition.pageNumber = refinedPage;
      } else {
        LOG_DBG("KOSync", "Paragraph %u not found in section LUT", remotePosition.paragraphIndex);
      }
    }
  }
  // localProgress was pre-computed in EpubReaderActivity before the Epub was released.

  {
    RenderLock lock(*this);
    state = SHOWING_RESULT;

    // Default to the option that corresponds to the furthest progress
    if (localProgress.percentage > remoteProgress.percentage) {
      selectedOption = 1;  // Upload local progress
    } else {
      selectedOption = 0;  // Apply remote progress
    }
  }
  requestUpdate(true);
}

void KOReaderSyncActivity::performUpload() {
  {
    RenderLock lock(*this);
    state = UPLOADING;
    statusMessage = tr(STR_UPLOAD_PROGRESS);
  }
  requestUpdateAndWait();

  // localProgress was pre-computed in EpubReaderActivity before the Epub was released.
  KOReaderProgress progress;
  progress.document = documentHash;
  progress.progress = localProgress.xpath;
  progress.percentage = localProgress.percentage;

  const auto result = KOReaderSyncClient::updateProgress(progress);

  if (result != KOReaderSyncClient::OK) {
    wifiOff();
    {
      RenderLock lock(*this);
      state = SYNC_FAILED;
      statusMessage = KOReaderSyncClient::errorString(result);
    }
    requestUpdate();
    return;
  }

  wifiOff();
  {
    RenderLock lock(*this);
    state = UPLOAD_COMPLETE;
  }
  requestUpdate(true);
}

void KOReaderSyncActivity::onEnter() {
  Activity::onEnter();
  ReaderUtils::applyOrientation(renderer, SETTINGS.orientation);

  // Check for credentials first
  if (!KOREADER_STORE.hasCredentials()) {
    state = NO_CREDENTIALS;
    requestUpdate();
    return;
  }

  // Check if already connected (e.g. from settings page auth)
  if (WiFi.status() == WL_CONNECTED && WiFi.localIP() != IPAddress(0, 0, 0, 0)) {
    LOG_DBG("KOSync", "Already connected to WiFi");
    onWifiSelectionComplete(true);
    return;
  }

  // Launch WiFi selection subactivity
  LOG_DBG("KOSync", "Launching WifiSelectionActivity...");
  startActivityForResult(std::make_unique<WifiSelectionActivity>(renderer, mappedInput),
                         [this](const ActivityResult& result) { onWifiSelectionComplete(!result.isCancelled); });
}

void KOReaderSyncActivity::onExit() {
  Activity::onExit();

  wifiOff();
}

void KOReaderSyncActivity::render(RenderLock&&) {
  renderer.clearScreen();
  const auto screen = AppScreenLayout::listScreen(renderer);
  const auto& m = UITheme::getInstance().getMetrics();

  GUI.drawHeader(renderer, screen.header, tr(STR_KOREADER_SYNC));

  if (state == NO_CREDENTIALS) {
    AppScreenLayout::drawBodyMessage(renderer, screen.body, tr(STR_NO_CREDENTIALS_MSG), true);
    const int hintY = screen.body.y + screen.body.height / 2 + renderer.getLineHeight(UI_10_FONT_ID);
    renderer.drawCenteredText(UI_10_FONT_ID, hintY, tr(STR_KOREADER_SETUP_HINT));
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer();
    return;
  }

  if (state == SYNCING || state == UPLOADING) {
    AppScreenLayout::drawBodyMessage(renderer, screen.body, statusMessage.c_str(), true);
    renderer.displayBuffer();
    return;
  }

  if (state == SHOWING_RESULT) {
    const int pad = m.contentSidePadding;
    const int textX = screen.body.x + pad;
    const int lineH = renderer.getLineHeight(UI_10_FONT_ID);
    int y = screen.body.y + 4;
    renderer.drawCenteredText(UI_10_FONT_ID, y, tr(STR_PROGRESS_FOUND), true, EpdFontFamily::BOLD);
    y += lineH + m.verticalSpacing;

    const int remoteTocIndex = epub->getTocIndexForSpineIndex(remotePosition.spineIndex);
    const std::string remoteChapter =
        (remoteTocIndex >= 0) ? epub->getTocItem(remoteTocIndex).title
                              : (std::string(tr(STR_SECTION_PREFIX)) + std::to_string(remotePosition.spineIndex + 1));
    const std::string localChapter =
        !localChapterName.empty() ? localChapterName
                                  : (std::string(tr(STR_SECTION_PREFIX)) + std::to_string(currentSpineIndex + 1));

    renderer.drawText(UI_10_FONT_ID, textX, y, tr(STR_REMOTE_LABEL), true);
    y += lineH;
    char remoteChapterStr[128];
    snprintf(remoteChapterStr, sizeof(remoteChapterStr), "  %s", remoteChapter.c_str());
    renderer.drawText(UI_10_FONT_ID, textX, y, remoteChapterStr);
    y += lineH;
    char remotePageStr[64];
    snprintf(remotePageStr, sizeof(remotePageStr), tr(STR_PAGE_OVERALL_FORMAT), remotePosition.pageNumber + 1,
             remoteProgress.percentage * 100);
    renderer.drawText(UI_10_FONT_ID, textX, y, remotePageStr);
    y += lineH;

    if (!remoteProgress.device.empty()) {
      char deviceStr[64];
      snprintf(deviceStr, sizeof(deviceStr), tr(STR_DEVICE_FROM_FORMAT), remoteProgress.device.c_str());
      renderer.drawText(UI_10_FONT_ID, textX, y, deviceStr);
      y += lineH + m.verticalSpacing / 2;
    } else {
      y += m.verticalSpacing / 2;
    }

    renderer.drawText(UI_10_FONT_ID, textX, y, tr(STR_LOCAL_LABEL), true);
    y += lineH;
    char localChapterStr[128];
    snprintf(localChapterStr, sizeof(localChapterStr), "  %s", localChapter.c_str());
    renderer.drawText(UI_10_FONT_ID, textX, y, localChapterStr);
    y += lineH;
    char localPageStr[64];
    snprintf(localPageStr, sizeof(localPageStr), tr(STR_PAGE_TOTAL_OVERALL_FORMAT), currentPage + 1, totalPagesInSpine,
             localProgress.percentage * 100);
    renderer.drawText(UI_10_FONT_ID, textX, y, localPageStr);

    const int optionsTop = screen.body.y + screen.body.height - m.listRowHeight * 2 - m.verticalSpacing;
    const Rect optionsRect{screen.body.x, optionsTop, screen.body.width,
                           m.listRowHeight * 2 + m.verticalSpacing};
    GUI.drawList(renderer, optionsRect, 2, selectedOption,
                 [](int index) { return index == 0 ? tr(STR_APPLY_REMOTE) : tr(STR_UPLOAD_LOCAL); });

    // Bottom button hints
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer();
    return;
  }

  if (state == NO_REMOTE_PROGRESS) {
    AppScreenLayout::drawBodyMessage(renderer, screen.body, tr(STR_NO_REMOTE_MSG), true);
    const int hintY = screen.body.y + screen.body.height / 2 + renderer.getLineHeight(UI_10_FONT_ID);
    renderer.drawCenteredText(UI_10_FONT_ID, hintY, tr(STR_UPLOAD_PROMPT));
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_UPLOAD), "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer();
    return;
  }

  if (state == UPLOAD_COMPLETE) {
    AppScreenLayout::drawBodyMessage(renderer, screen.body, tr(STR_UPLOAD_SUCCESS), true);
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer();
    return;
  }

  if (state == SYNC_FAILED) {
    AppScreenLayout::drawBodyMessage(renderer, screen.body, tr(STR_SYNC_FAILED_MSG), true);
    const int hintY = screen.body.y + screen.body.height / 2 + renderer.getLineHeight(UI_10_FONT_ID);
    renderer.drawCenteredText(UI_10_FONT_ID, hintY, statusMessage.c_str());
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer();
    return;
  }
}

void KOReaderSyncActivity::loop() {
  if (state == NO_CREDENTIALS || state == SYNC_FAILED || state == UPLOAD_COMPLETE) {
    if (ReaderUtils::wasShortBackClicked(mappedInput)) {
      returnToReader();
    }
    return;
  }

  if (state == SHOWING_RESULT) {
    // Navigate options
    if (mappedInput.wasReleased(MappedInputManager::Button::Up) ||
        mappedInput.wasReleased(MappedInputManager::Button::Left)) {
      selectedOption = (selectedOption + 2 - 1) % 2;
      requestUpdate();
    } else if (mappedInput.wasReleased(MappedInputManager::Button::Down) ||
               mappedInput.wasReleased(MappedInputManager::Button::Right)) {
      selectedOption = (selectedOption + 1) % 2;
      requestUpdate();
    }

    if (mappedInput.wasConfirmClicked()) {
      if (selectedOption == 0) {
        saveProgressAndReturn(remotePosition.spineIndex, remotePosition.pageNumber);
      } else if (selectedOption == 1) {
        // Upload local progress
        performUpload();
      }
    }

    if (ReaderUtils::wasShortBackClicked(mappedInput)) {
      returnToReader();
    }
    return;
  }

  if (state == NO_REMOTE_PROGRESS) {
    if (mappedInput.wasConfirmClicked()) {
      // Calculate hash if not done yet
      if (documentHash.empty()) {
        if (KOREADER_STORE.getMatchMethod() == DocumentMatchMethod::FILENAME) {
          documentHash = KOReaderDocumentId::calculateFromFilename(epubPath);
        } else {
          documentHash = KOReaderDocumentId::calculate(epubPath);
        }
      }
      performUpload();
    }

    if (ReaderUtils::wasShortBackClicked(mappedInput)) {
      returnToReader();
    }
    return;
  }
}
