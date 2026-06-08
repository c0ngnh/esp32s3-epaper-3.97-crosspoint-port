#include "FileBrowserActivity.h"

#include <Epub.h>
#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>
#if defined(BOARD_ESP32_S3_EPAPER_397)
#include <SDCardManager.h>
#include <SdMountPath.h>
#endif

#include <algorithm>

#include "CrossPointSettings.h"
#include "HalGPIO.h"
#include "MappedInputManager.h"
#include "activities/util/ConfirmationActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
constexpr unsigned long GO_HOME_MS = 1000;

// SD_MMC may return a full path at card root (e.g. "/sdcard/book.epub") but only the leaf in subfolders.
std::string listEntryLeaf(const char* rawName) {
  if (rawName == nullptr || rawName[0] == '\0') {
    return {};
  }
  std::string s(rawName);
  while (!s.empty() && s.back() == '/') {
    s.pop_back();
  }
  const auto pos = s.find_last_of('/');
  if (pos == std::string::npos) {
    return s;
  }
  return s.substr(pos + 1);
}

// Join app-visible paths ("/" = SD root; resolveSdMountPath adds /sdcard on 397).
std::string joinAppPath(const std::string& dir, const std::string& entry) {
  const std::string leaf = listEntryLeaf(entry.c_str());
  if (leaf.empty()) {
    return dir.empty() ? "/" : dir;
  }
  if (dir.empty() || dir == "/") {
    return "/" + leaf;
  }
  std::string out = dir;
  if (out.back() != '/') {
    out += '/';
  }
  out += leaf;
  return out;
}
}  // namespace

void FileBrowserActivity::loadFiles() {
  files.clear();

#if defined(BOARD_ESP32_S3_EPAPER_397)
  auto root = Storage.open(basepath.c_str());
  if (!root) {
    SDCardManager::getInstance().remount();
    root = Storage.open(basepath.c_str());
  }
#else
  auto root = Storage.open(basepath.c_str());
#endif
  if (!root || !root.isDirectory()) {
#if defined(BOARD_ESP32_S3_EPAPER_397)
    LOG_ERR("FileBrowser", "Cannot open directory '%s' (resolved '%s')", basepath.c_str(),
            resolveSdMountPath(basepath.c_str()).c_str());
#endif
    return;
  }

#if !defined(BOARD_ESP32_S3_EPAPER_397)
  // rewindDirectory() breaks SDIO openNextFile() on Waveshare 397 — skip it there.
  root.rewindDirectory();
#endif

  char name[500];
  int rawEntryCount = 0;
  while (true) {
    auto file = root.openNextFile();
    if (!file) {
      break;
    }
    file.getName(name, sizeof(name));
    rawEntryCount++;
    const std::string leaf = listEntryLeaf(name);
    if (leaf.empty() || (!SETTINGS.showHiddenFiles && leaf[0] == '.') ||
        leaf == "System Volume Information") {
      continue;
    }

    if (file.isDirectory()) {
      files.emplace_back(leaf + "/");
    } else if (mode == Mode::PickFirmware) {
      // Firmware picker: only show .bin files.
      std::string_view filename{leaf};
      if (FsHelpers::checkFileExtension(filename, ".bin")) {
        files.emplace_back(std::string(filename));
      }
    } else {
      // Books browser: show all files (e.g. .wav in /recordings, docs, logs).
      files.emplace_back(leaf);
    }
    file.close();
  }
  root.close();
  FsHelpers::sortFileList(files);
#if defined(BOARD_ESP32_S3_EPAPER_397)
  if (files.empty()) {
    LOG_DBG("FileBrowser", "No entries in '%s' (mount '%s', raw=%d)", basepath.c_str(),
            resolveSdMountPath(basepath.c_str()).c_str(), rawEntryCount);
  }
#endif
}

void FileBrowserActivity::onSelectBook(const std::string& path) { activityManager.pushReader(path); }

void FileBrowserActivity::onEnter() {
  Activity::onEnter();

#if defined(BOARD_ESP32_S3_EPAPER_397)
  gestureSettleUntilMs = millis() + 800;
#endif

#if !defined(BOARD_ESP32_S3_EPAPER_397)
  // If Confirm was held while this activity opened (typical when launched from a menu), ignore
  // its release — otherwise we'd immediately auto-open whatever is at index 0.
  lockNextConfirmRelease = mappedInput.isPressed(MappedInputManager::Button::Confirm);
#endif

  auto root = Storage.open(basepath.c_str());
  if (!root) {
#if defined(BOARD_ESP32_S3_EPAPER_397)
    LOG_ERR("FileBrowser", "Storage.open failed for '%s' (resolved '%s')", basepath.c_str(),
            resolveSdMountPath(basepath.c_str()).c_str());
#endif
    basepath = "/";
    loadFiles();
    selectorIndex = 0;
  } else if (!root.isDirectory()) {
#if defined(BOARD_ESP32_S3_EPAPER_397)
    lockLongPressBack = mappedInput.isPressed(MappedInputManager::Button::Confirm) &&
                        mappedInput.getHeldTime() >= GO_HOME_MS;
#else
    lockLongPressBack = mappedInput.isPressed(MappedInputManager::Button::Back);
#endif

    const std::string oldPath = basepath;
    basepath = FsHelpers::extractFolderPath(basepath);
    loadFiles();

    const auto pos = oldPath.find_last_of('/');
    const std::string fileName = oldPath.substr(pos + 1);
    const int found = findEntry(fileName);
    selectorIndex = found >= 0 ? static_cast<size_t>(found) : 0;
  } else {
    loadFiles();
    if (files.empty()) {
      selectorIndex = 0;
    } else if (selectorIndex >= files.size()) {
      selectorIndex = files.size() - 1;
    }
  }

  requestUpdate();
}

void FileBrowserActivity::onExit() {
  Activity::onExit();
  files.clear();
}

void FileBrowserActivity::clearFileMetadata(const std::string& fullPath) {
  // Only clear cache for .epub files
  if (FsHelpers::hasEpubExtension(fullPath)) {
    Epub(fullPath, "/.crosspoint").clearCache();
    LOG_DBG("FileBrowser", "Cleared metadata cache for: %s", fullPath.c_str());
  }
}

void FileBrowserActivity::loop() {
  // Long press center (1s+) goes to SD root. Disabled on 3.97" — center is OK; BOOT is Back.
#if !defined(BOARD_ESP32_S3_EPAPER_397)
  const bool longPressCenter = mappedInput.isPressed(MappedInputManager::Button::Back) &&
                               mappedInput.getHeldTime() >= GO_HOME_MS;
  if (mode == Mode::Books && longPressCenter && basepath != "/" && !lockLongPressBack) {
    basepath = "/";
    loadFiles();
    selectorIndex = 0;
    requestUpdate();
    return;
  }
#endif

  if (lockLongPressBack) {
    if (mappedInput.wasBackClicked()) {
      lockLongPressBack = false;
      return;
    }
#if defined(BOARD_ESP32_S3_EPAPER_397)
    if (mappedInput.wasConfirmClicked()) {
      lockLongPressBack = false;
    }
#endif
  }

  const int pathReserved = renderer.getLineHeight(SMALL_FONT_ID) + UITheme::getInstance().getMetrics().verticalSpacing;
  const int pageItems = UITheme::getNumberOfItemsPerPage(renderer, true, false, true, false, pathReserved);

#if defined(BOARD_ESP32_S3_EPAPER_397)
  if (consumeConfirmClick()) {
#else
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    if (lockNextConfirmRelease) {
      lockNextConfirmRelease = false;
      return;
    }
#endif
    if (files.empty() || selectorIndex >= files.size()) return;

    const std::string& entry = files[selectorIndex];
    bool isDirectory = (entry.back() == '/');

    // Firmware picker: select file -> return path; navigate into directories normally.
    if (mode == Mode::PickFirmware && !isDirectory) {
      ActivityResult res{FilePathResult{joinAppPath(basepath, entry)}};
      res.isCancelled = false;
      setResult(std::move(res));
      finish();
      return;
    }

#if !defined(BOARD_ESP32_S3_EPAPER_397)
    if (mode == Mode::Books && mappedInput.getHeldTime() >= GO_HOME_MS) {
      // --- LONG PRESS ACTION: DELETE FILE OR DIRECTORY ---
      std::string cleanBasePath = basepath;
      if (cleanBasePath.back() != '/') cleanBasePath += "/";
      const std::string fullPath = cleanBasePath + entry;

      auto handler = [this, fullPath, isDirectory](const ActivityResult& res) {
        if (!res.isCancelled) {
          LOG_DBG("FileBrowser", "Attempting to delete: %s", fullPath.c_str());
          if (!isDirectory) {
            clearFileMetadata(fullPath);
          }
          const bool deleted = isDirectory ? Storage.removeDir(fullPath.c_str()) : Storage.remove(fullPath.c_str());
          if (deleted) {
            LOG_DBG("FileBrowser", "Deleted successfully");
            loadFiles();
            if (files.empty()) {
              selectorIndex = 0;
            } else if (selectorIndex >= files.size()) {
              // Move selection to the new "last" item
              selectorIndex = files.size() - 1;
            }

            requestUpdate(true);
          } else {
            LOG_ERR("FileBrowser", "Failed to delete: %s", fullPath.c_str());
          }
        } else {
          LOG_DBG("FileBrowser", "Delete cancelled by user");
        }
      };

      std::string heading = tr(STR_DELETE) + std::string("? ");

      startActivityForResult(std::make_unique<ConfirmationActivity>(renderer, mappedInput, heading, entry), handler);
      return;
    } else
#endif
    {
      // --- SHORT PRESS ACTION: OPEN/NAVIGATE ---
      if (isDirectory) {
        basepath = joinAppPath(basepath, entry);
        loadFiles();
        selectorIndex = 0;
        requestUpdate();
      } else {
        onSelectBook(joinAppPath(basepath, entry));
      }
    }
    return;
  }

#if defined(BOARD_ESP32_S3_EPAPER_397)
  if (mode == Mode::Books && millis() >= gestureSettleUntilMs && !files.empty() &&
      selectorIndex < files.size() && consumeShakeDeleteRequest()) {
    const std::string& entry = files[selectorIndex];
    const bool isDirectory = (entry.back() == '/');
    std::string cleanBasePath = basepath;
    if (cleanBasePath.back() != '/') cleanBasePath += "/";
    const std::string fullPath = cleanBasePath + entry;

    auto handler = [this, fullPath, isDirectory](const ActivityResult& res) {
      if (!res.isCancelled) {
        if (!isDirectory) {
          clearFileMetadata(fullPath);
        }
        const bool deleted = isDirectory ? Storage.removeDir(fullPath.c_str()) : Storage.remove(fullPath.c_str());
        if (deleted) {
          loadFiles();
          if (files.empty()) {
            selectorIndex = 0;
          } else if (selectorIndex >= files.size()) {
            selectorIndex = files.size() - 1;
          }
          requestUpdate(true);
        } else {
          LOG_ERR("FileBrowser", "Failed to delete: %s", fullPath.c_str());
        }
      }
    };

    const std::string heading = std::string(tr(STR_DELETE)) + "? ";
    startActivityForResult(
        std::make_unique<ConfirmationActivity>(renderer, mappedInput, heading, entry, true), handler);
    return;
  }

  if (mappedInput.wasBackClicked()) {
    armBackGestureLock();
    if (basepath != "/") {
      const std::string oldPath = basepath;

      basepath.replace(basepath.find_last_of('/'), std::string::npos, "");
      if (basepath.empty()) basepath = "/";
      loadFiles();

      const auto pos = oldPath.find_last_of('/');
      const std::string dirName = oldPath.substr(pos + 1) + "/";
      const int found = findEntry(dirName);
      selectorIndex = found >= 0 ? static_cast<size_t>(found) : 0;

      requestUpdate();
    } else if (mode == Mode::PickFirmware) {
      ActivityResult res;
      res.isCancelled = true;
      setResult(std::move(res));
      finish();
    } else {
      activityManager.goHome();
    }
    return;
  }
#endif

#if !defined(BOARD_ESP32_S3_EPAPER_397)
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    // Short press: go up one directory, or go home if at root
    if (mappedInput.getHeldTime() < GO_HOME_MS) {
      if (basepath != "/") {
        const std::string oldPath = basepath;

        basepath.replace(basepath.find_last_of('/'), std::string::npos, "");
        if (basepath.empty()) basepath = "/";
        loadFiles();

        const auto pos = oldPath.find_last_of('/');
        const std::string dirName = oldPath.substr(pos + 1) + "/";
        const int found = findEntry(dirName);
      selectorIndex = found >= 0 ? static_cast<size_t>(found) : 0;

        requestUpdate();
      } else if (mode == Mode::PickFirmware) {
        ActivityResult res;
        res.isCancelled = true;
        setResult(std::move(res));
        finish();
      } else {
        activityManager.goHome();
      }
    }
  }
#endif

  int listSize = static_cast<int>(files.size());
  buttonNavigator.onNextRelease([this, listSize] {
    selectorIndex = ButtonNavigator::nextIndex(static_cast<int>(selectorIndex), listSize);
    requestUpdate();
  });

  buttonNavigator.onPreviousRelease([this, listSize] {
    selectorIndex = ButtonNavigator::previousIndex(static_cast<int>(selectorIndex), listSize);
    requestUpdate();
  });

  buttonNavigator.onNextContinuous([this, listSize, pageItems] {
    selectorIndex = ButtonNavigator::nextPageIndex(static_cast<int>(selectorIndex), listSize, pageItems);
    requestUpdate();
  });

  buttonNavigator.onPreviousContinuous([this, listSize, pageItems] {
    selectorIndex = ButtonNavigator::previousPageIndex(static_cast<int>(selectorIndex), listSize, pageItems);
    requestUpdate();
  });
}

std::string getFileName(std::string filename) {
  if (filename.back() == '/') {
    filename.pop_back();
    if (!UITheme::getInstance().getTheme().showsFileIcons()) {
      return "[" + filename + "]";
    }
    return filename;
  }
  const auto pos = filename.rfind('.');
  if (pos == std::string::npos) {
    return filename;
  }
  return filename.substr(0, pos);
}

std::string getFileExtension(std::string filename) {
  if (filename.back() == '/') {
    return "";
  }
  const auto pos = filename.rfind('.');
  if (pos == std::string::npos) {
    return "";
  }
  return filename.substr(pos);
}

void FileBrowserActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  const auto& metrics = UITheme::getInstance().getMetrics();

  std::string folderName =
      (mode == Mode::PickFirmware)
          ? std::string(tr(STR_SELECT_FIRMWARE_FILE))
          : ((basepath == "/") ? std::string(tr(STR_SD_CARD)) : basepath.substr(basepath.rfind('/') + 1));
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, folderName.c_str());

  const int pathLineHeight = renderer.getLineHeight(SMALL_FONT_ID);
  const int pathReserved = pathLineHeight + metrics.verticalSpacing;
  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight = std::max(
      0, pageHeight - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing - pathReserved);
  if (files.empty()) {
    const char* emptyMsg = (mode == Mode::PickFirmware) ? tr(STR_NO_BIN_FILES) : tr(STR_NO_FILES_FOUND);
    renderer.drawText(UI_10_FONT_ID, metrics.contentSidePadding, contentTop + 20, emptyMsg);
  } else {
    const int safeIndex = static_cast<int>(std::min(selectorIndex, files.size() - 1));
    GUI.drawList(
        renderer, Rect{0, contentTop, pageWidth, contentHeight}, static_cast<int>(files.size()), safeIndex,
        [this](int index) { return getFileName(files[index]); }, nullptr,
        [this](int index) { return UITheme::getFileIcon(files[index]); },
        [this](int index) { return getFileExtension(files[index]); }, false);
  }

  // Full path display
  {
    const int pathY = pageHeight - metrics.buttonHintsHeight - metrics.verticalSpacing - pathLineHeight;
    const int separatorY = pathY - metrics.verticalSpacing / 2;
    renderer.drawLine(0, separatorY, pageWidth - 1, separatorY, 3, true);
    const int pathMaxWidth = pageWidth - metrics.contentSidePadding * 2;
    // Left-truncate so the deepest directory is always visible
    const char* pathStr = basepath.c_str();
    const char* pathDisplay = pathStr;
    char leftTruncBuf[256];
    if (renderer.getTextWidth(SMALL_FONT_ID, pathStr) > pathMaxWidth) {
      const char ellipsis[] = "\xe2\x80\xa6";  // UTF-8 ellipsis (…)
      const int ellipsisWidth = renderer.getTextWidth(SMALL_FONT_ID, ellipsis);
      const int available = pathMaxWidth - ellipsisWidth;
      // Walk forward from the start until the suffix fits, skipping UTF-8 continuation bytes
      const char* p = pathStr;
      while (*p) {
        if (renderer.getTextWidth(SMALL_FONT_ID, p) <= available) break;
        ++p;
        while (*p && (static_cast<unsigned char>(*p) & 0xC0) == 0x80) ++p;
      }
      snprintf(leftTruncBuf, sizeof(leftTruncBuf), "%s%s", ellipsis, p);
      pathDisplay = leftTruncBuf;
    }
    renderer.drawText(SMALL_FONT_ID, metrics.contentSidePadding, pathY, pathDisplay);
  }

  // Help text
  const char* backLabel = (basepath == "/") ? (mode == Mode::PickFirmware ? tr(STR_BACK) : tr(STR_HOME)) : tr(STR_BACK);
  // In PickFirmware mode, Confirm on a .bin returns the path to the caller (not "open"); show
  // STR_SELECT instead. Directories in the same picker still descend, so keep STR_OPEN there.
  const size_t hintIndex = files.empty() ? 0 : std::min(selectorIndex, files.size() - 1);
  const bool selectingFirmwareFile =
      mode == Mode::PickFirmware && !files.empty() && files[hintIndex].back() != '/';
  const char* confirmLabel = files.empty() ? "" : (selectingFirmwareFile ? tr(STR_SELECT) : tr(STR_OPEN));
  const auto labels = mappedInput.mapLabels(backLabel, confirmLabel, files.empty() ? "" : tr(STR_DIR_UP),
                                            files.empty() ? "" : tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}

int FileBrowserActivity::findEntry(const std::string& name) const {
  for (size_t i = 0; i < files.size(); i++) {
    if (files[i] == name) {
      return static_cast<int>(i);
    }
  }
  return -1;
}
