#include "HomeActivity.h"

#include <Bitmap.h>
#include <Epub.h>
#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Utf8.h>
#include <Xtc.h>

#include <algorithm>
#include <cstring>
#include <vector>

#include "CrossPointSettings.h"
#include "CrossPointState.h"
#include "MappedInputManager.h"
#include "OpdsServerStore.h"
#include "RecentBooksStore.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {

// Survives goHome() creating a new HomeActivity (remember highlight when backing out of apps).
int g_homeLastSelectorIndex = 0;

}  // namespace

int HomeActivity::getRecentCoverSlotCount() const {
  if (recentBooks.empty()) {
    return 0;
  }
  const auto& metrics = UITheme::getInstance().getMetrics();
  if (metrics.homeContinueReadingInMenu) {
    return 0;
  }
  return std::min(static_cast<int>(recentBooks.size()), metrics.homeRecentBooksCount);
}

int HomeActivity::getMenuOnlyCount() const { return static_cast<int>(menuEntries.size()); }

int HomeActivity::getMenuItemCount() const { return getRecentCoverSlotCount() + getMenuOnlyCount(); }

int HomeActivity::getMenuRowIndex() const {
  const auto& metrics = UITheme::getInstance().getMetrics();
  if (metrics.homeContinueReadingInMenu) {
    return selectorIndex;
  }
  return selectorIndex - getRecentCoverSlotCount();
}

void HomeActivity::rebuildMenuEntries() {
  menuEntries.clear();
  const auto& metrics = UITheme::getInstance().getMetrics();

  if (metrics.homeContinueReadingInMenu && !recentBooks.empty()) {
    menuEntries.push_back({HomeMenuAction::ContinueReading, tr(STR_CONTINUE_READING), Book});
  }
  menuEntries.push_back({HomeMenuAction::BrowseFiles, tr(STR_BROWSE_FILES), Folder});
  menuEntries.push_back({HomeMenuAction::RecentBooks, tr(STR_MENU_RECENT_BOOKS), Recent});
#if defined(BOARD_ESP32_S3_EPAPER_397)
  menuEntries.push_back({HomeMenuAction::Pictures, tr(STR_PICTURES), Image});
  menuEntries.push_back({HomeMenuAction::Music, tr(STR_MUSIC_PLAYER), Music});
  menuEntries.push_back({HomeMenuAction::VoiceRecorder, tr(STR_VOICE_RECORDER), Mic});
  menuEntries.push_back({HomeMenuAction::Calendar, tr(STR_CALENDAR_APP), Calendar});
  menuEntries.push_back({HomeMenuAction::Clock, tr(STR_CLOCK_APP), Clock});
#endif
  if (hasOpdsServers) {
    menuEntries.push_back({HomeMenuAction::OpdsBrowser, tr(STR_OPDS_BROWSER), Library});
  }
  menuEntries.push_back({HomeMenuAction::FileTransfer, tr(STR_FILE_TRANSFER), Transfer});
  menuEntries.push_back({HomeMenuAction::Settings, tr(STR_SETTINGS_TITLE), Settings});
}

void HomeActivity::runMenuAction(const HomeMenuAction action) {
  switch (action) {
    case HomeMenuAction::ContinueReading:
      if (!recentBooks.empty()) {
        onSelectBook(recentBooks[0].path);
      }
      break;
    case HomeMenuAction::BrowseFiles:
      onFileBrowserOpen();
      break;
    case HomeMenuAction::RecentBooks:
      onRecentsOpen();
      break;
#if defined(BOARD_ESP32_S3_EPAPER_397)
    case HomeMenuAction::Pictures:
      onPicturesOpen();
      break;
    case HomeMenuAction::Music:
      onMusicPlayerOpen();
      break;
    case HomeMenuAction::VoiceRecorder:
      onVoiceRecorderOpen();
      break;
    case HomeMenuAction::Calendar:
      onCalendarOpen();
      break;
    case HomeMenuAction::Clock:
      onClockOpen();
      break;
#endif
    case HomeMenuAction::OpdsBrowser:
      onOpdsBrowserOpen();
      break;
    case HomeMenuAction::FileTransfer:
      onFileTransferOpen();
      break;
    case HomeMenuAction::Settings:
      onSettingsOpen();
      break;
  }
}

void HomeActivity::handleMenuConfirm(const int menuRow) {
  if (menuRow < 0 || menuRow >= static_cast<int>(menuEntries.size())) {
    return;
  }
  runMenuAction(menuEntries[menuRow].action);
}

void HomeActivity::loadRecentBooks(int maxBooks) {
  recentBooks.clear();
  const auto& books = RECENT_BOOKS.getBooks();
  recentBooks.reserve(std::min(static_cast<int>(books.size()), maxBooks));

  for (const RecentBook& book : books) {
    // Limit to maximum number of recent books
    if (recentBooks.size() >= maxBooks) {
      break;
    }

    // Skip if file no longer exists
    if (!Storage.exists(book.path.c_str())) {
      continue;
    }

    recentBooks.push_back(book);
  }
}

void HomeActivity::loadRecentCovers(int coverHeight) {
  recentsLoading = true;
  bool showingLoading = false;
  Rect popupRect;

  int progress = 0;
  for (RecentBook& book : recentBooks) {
    if (!book.coverBmpPath.empty()) {
      std::string coverPath = UITheme::getCoverThumbPath(book.coverBmpPath, coverHeight);
      if (!Storage.exists(coverPath.c_str())) {
        // If epub, try to load the metadata for title/author and cover
        if (FsHelpers::hasEpubExtension(book.path)) {
          Epub epub(book.path, "/.crosspoint");
          // Skip loading css since we only need metadata here
          epub.load(false, true);

          // Try to generate thumbnail image for Continue Reading card
          if (!showingLoading) {
            showingLoading = true;
            popupRect = GUI.drawPopup(renderer, tr(STR_LOADING_POPUP));
          }
          GUI.fillPopupProgress(renderer, popupRect, 10 + progress * (90 / recentBooks.size()));
          bool success = epub.generateThumbBmp(coverHeight);
          if (!success) {
            RECENT_BOOKS.updateBook(book.path, book.title, book.author, "");
            book.coverBmpPath = "";
          }
          coverRendered = false;
          requestUpdate();
        } else if (FsHelpers::hasXtcExtension(book.path)) {
          // Handle XTC file
          Xtc xtc(book.path, "/.crosspoint");
          if (xtc.load()) {
            // Try to generate thumbnail image for Continue Reading card
            if (!showingLoading) {
              showingLoading = true;
              popupRect = GUI.drawPopup(renderer, tr(STR_LOADING_POPUP));
            }
            GUI.fillPopupProgress(renderer, popupRect, 10 + progress * (90 / recentBooks.size()));
            bool success = xtc.generateThumbBmp(coverHeight);
            if (!success) {
              RECENT_BOOKS.updateBook(book.path, book.title, book.author, "");
              book.coverBmpPath = "";
            }
            coverRendered = false;
            requestUpdate();
          }
        }
      }
    }
    progress++;
  }

  recentsLoaded = true;
  recentsLoading = false;
  rebuildMenuEntries();
}

void HomeActivity::onEnter() {
  Activity::onEnter();

  hasOpdsServers = OPDS_STORE.hasServers();

  const auto& metrics = UITheme::getInstance().getMetrics();
  loadRecentBooks(metrics.homeRecentBooksCount);
  rebuildMenuEntries();

  const int menuCount = getMenuItemCount();
  if (menuCount > 0) {
    selectorIndex = std::clamp(g_homeLastSelectorIndex, 0, menuCount - 1);
  } else {
    selectorIndex = 0;
  }

  // Trigger first update
  requestUpdate();
}

void HomeActivity::onExit() {
  g_homeLastSelectorIndex = selectorIndex;

  Activity::onExit();

  // Free the stored cover buffer if any
  freeCoverBuffer();
}

bool HomeActivity::storeCoverBuffer() {
  uint8_t* frameBuffer = renderer.getFrameBuffer();
  if (!frameBuffer) {
    return false;
  }

  // Free any existing buffer first
  freeCoverBuffer();

  const size_t bufferSize = renderer.getBufferSize();
  coverBuffer = static_cast<uint8_t*>(malloc(bufferSize));
  if (!coverBuffer) {
    return false;
  }

  memcpy(coverBuffer, frameBuffer, bufferSize);
  return true;
}

bool HomeActivity::restoreCoverBuffer() {
  if (!coverBuffer) {
    return false;
  }

  uint8_t* frameBuffer = renderer.getFrameBuffer();
  if (!frameBuffer) {
    return false;
  }

  const size_t bufferSize = renderer.getBufferSize();
  memcpy(frameBuffer, coverBuffer, bufferSize);
  return true;
}

void HomeActivity::freeCoverBuffer() {
  if (coverBuffer) {
    free(coverBuffer);
    coverBuffer = nullptr;
  }
  coverBufferStored = false;
}

void HomeActivity::loop() {
  if (!recentsLoaded && !recentsLoading) {
    recentsLoading = true;
    const auto& metrics = UITheme::getInstance().getMetrics();
    loadRecentCovers(metrics.homeCoverHeight);
    requestUpdate();
  }

  const int menuCount = getMenuItemCount();

  buttonNavigator.onNext([this, menuCount] {
    selectorIndex = ButtonNavigator::nextIndex(selectorIndex, menuCount);
    requestUpdate();
  });

  buttonNavigator.onPrevious([this, menuCount] {
    selectorIndex = ButtonNavigator::previousIndex(selectorIndex, menuCount);
    requestUpdate();
  });

#if defined(BOARD_ESP32_S3_EPAPER_397)
  if (mappedInput.wasConfirmClicked()) {
#else
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
#endif
    const int coverSlots = getRecentCoverSlotCount();
    if (selectorIndex < coverSlots) {
      onSelectBook(recentBooks[selectorIndex].path);
    } else {
      handleMenuConfirm(getMenuRowIndex());
    }
  }
}

void HomeActivity::render(RenderLock&&) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  renderer.clearScreen();
  bool bufferRestored = coverBufferStored && restoreCoverBuffer();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.homeTopPadding},
                 metrics.homeContinueReadingInMenu && !recentBooks.empty() ? recentBooks[0].title.c_str() : nullptr);

  const int coverSlots = getRecentCoverSlotCount();
  const int coverSelector = (selectorIndex < coverSlots) ? selectorIndex : -1;
  GUI.drawRecentBookCover(renderer, Rect{0, metrics.homeTopPadding, pageWidth, metrics.homeCoverTileHeight},
                          recentBooks, coverSelector, coverRendered, coverBufferStored, bufferRestored,
                          std::bind(&HomeActivity::storeCoverBuffer, this));

  const int menuTop =
      metrics.homeTopPadding + metrics.homeCoverTileHeight + metrics.homeMenuTopOffset;
  const int menuBottomReserve = metrics.buttonHintsHeight + metrics.verticalSpacing;
  const int menuHeight = std::max(0, pageHeight - menuTop - menuBottomReserve);

  const int menuRow = getMenuRowIndex();
  const int menuSelector = (selectorIndex >= coverSlots) ? menuRow : -1;
  GUI.drawButtonMenu(renderer, Rect{0, menuTop, pageWidth, menuHeight}, static_cast<int>(menuEntries.size()),
                     menuSelector,
                     [this](int index) { return menuEntries[index].label; },
                     [this](int index) { return menuEntries[index].icon; }, 7);

  const auto labels = mappedInput.mapLabels("", tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();

  if (!firstRenderDone) {
    firstRenderDone = true;
    requestUpdate();
  }
}

void HomeActivity::onSelectBook(const std::string& path) { activityManager.goToReader(path); }

void HomeActivity::onFileBrowserOpen() { activityManager.goToFileBrowser(); }

void HomeActivity::onRecentsOpen() { activityManager.goToRecentBooks(); }

void HomeActivity::onSettingsOpen() { activityManager.goToSettings(); }

void HomeActivity::onFileTransferOpen() { activityManager.goToFileTransfer(); }

void HomeActivity::onOpdsBrowserOpen() { activityManager.goToBrowser(); }

#if defined(BOARD_ESP32_S3_EPAPER_397)
void HomeActivity::onPicturesOpen() { activityManager.goToPictures(); }

void HomeActivity::onMusicPlayerOpen() { activityManager.goToMusicPlayer(); }

void HomeActivity::onVoiceRecorderOpen() { activityManager.goToVoiceRecorder(); }

void HomeActivity::onCalendarOpen() { activityManager.goToCalendar(); }

void HomeActivity::onClockOpen() { activityManager.goToClock(); }
#endif
