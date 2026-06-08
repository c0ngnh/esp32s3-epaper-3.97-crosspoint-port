#include "BmpViewerActivity.h"

#include <esp_task_wdt.h>

#include <Bitmap.h>
#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include "ImageToFramebufferDecoder.h"
#include "JpegToFramebufferConverter.h"
#include "PngToFramebufferConverter.h"

#include <algorithm>
#include <cstdint>
#include <cstring>

#include "CrossPointSettings.h"
#include "activities/reader/ReaderUtils.h"
#include "activities/util/ConfirmationActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/ImageFitUtil.h"

namespace {

bool isViewerImageExtension(const std::string& name) {
  return FsHelpers::hasBmpExtension(name) || FsHelpers::hasPngExtension(name) || FsHelpers::hasJpgExtension(name);
}

}  // namespace

BmpViewerActivity::BmpViewerActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string path)
    : Activity("BmpViewer", renderer, mappedInput), filePath(std::move(path)) {}

void BmpViewerActivity::loadSiblingImages() {
  siblingImages.clear();
  currentImageIndex = -1;

  if (filePath.empty()) return;

  std::string dirPath = FsHelpers::extractFolderPath(filePath);
  size_t lastSlash = filePath.find_last_of('/');
  std::string fileName = (lastSlash != std::string::npos) ? filePath.substr(lastSlash + 1) : filePath;

  auto dir = Storage.open(dirPath.c_str());
  if (!dir || !dir.isDirectory()) {
    if (dir) dir.close();
    return;
  }

  char name[500];
  for (auto file = dir.openNextFile(); file; file = dir.openNextFile()) {
    if (!file.isDirectory()) {
      file.getName(name, sizeof(name));
      if (name[0] != '.') {
        std::string fname(name);
        const size_t slash = fname.find_last_of('/');
        if (slash != std::string::npos) {
          fname = fname.substr(slash + 1);
        }
        if (isViewerImageExtension(fname)) {
          siblingImages.push_back(fname);
        }
      }
    }
    file.close();
  }
  dir.close();

  FsHelpers::sortFileList(siblingImages);

  for (size_t i = 0; i < siblingImages.size(); ++i) {
    if (siblingImages[i] == fileName) {
      currentImageIndex = static_cast<int>(i);
      break;
    }
  }
}

bool BmpViewerActivity::getImageDimensions(int& width, int& height) {
  if (FsHelpers::hasBmpExtension(filePath)) {
    FsFile file;
    if (!Storage.openFileForRead("IMG", filePath, file)) {
      return false;
    }
    Bitmap bitmap(file, true);
    if (bitmap.parseHeaders() != BmpReaderError::Ok) {
      return false;
    }
    width = bitmap.getWidth();
    height = bitmap.getHeight();
    return width > 0 && height > 0;
  }

  ImageDimensions dims{};
  if (FsHelpers::hasPngExtension(filePath)) {
    if (!PngToFramebufferConverter::getDimensionsStatic(filePath, dims)) {
      return false;
    }
  } else if (FsHelpers::hasJpgExtension(filePath)) {
    if (!JpegToFramebufferConverter::getDimensionsStatic(filePath, dims)) {
      return false;
    }
  } else {
    return false;
  }
  width = dims.width;
  height = dims.height;
  return width > 0 && height > 0;
}

bool BmpViewerActivity::renderImage() {
  int imgW = 0;
  int imgH = 0;
  if (!getImageDimensions(imgW, imgH)) {
    return false;
  }

  const ImageFit fit = pickBestImageFit(renderer, imgW, imgH);
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  if (FsHelpers::hasBmpExtension(filePath)) {
    FsFile file;
    if (!Storage.openFileForRead("IMG", filePath, file)) {
      return false;
    }
    Bitmap bitmap(file, true);
    if (bitmap.parseHeaders() != BmpReaderError::Ok) {
      return false;
    }
    renderer.drawBitmap(bitmap, fit.x, fit.y, pageWidth, pageHeight, 0, 0);
    return true;
  }

  RenderConfig config{};
  config.x = fit.x;
  config.y = fit.y;
  config.maxWidth = fit.drawW;
  config.maxHeight = fit.drawH;
  config.useGrayscale = true;
  config.useDithering = true;
  config.useExactDimensions = true;

  if (FsHelpers::hasPngExtension(filePath)) {
    PngToFramebufferConverter png;
    return png.decodeToFramebuffer(filePath, renderer, config);
  }
  JpegToFramebufferConverter jpeg;
  return jpeg.decodeToFramebuffer(filePath, renderer, config);
}

void BmpViewerActivity::doDeleteImage() {
  if (filePath.empty()) return;

  const std::string pathToDelete = filePath;
  auto handler = [this, pathToDelete](const ActivityResult& res) {
    if (res.isCancelled) {
      reloadView();
      return;
    }
    if (!Storage.remove(pathToDelete.c_str())) {
      GUI.drawPopup(renderer, tr(STR_FAILED_LOWER));
      delay(1000);
      reloadView();
      return;
    }
    loadSiblingImages();
    if (siblingImages.empty()) {
      activityManager.goToFileBrowser(FsHelpers::extractFolderPath(pathToDelete));
      return;
    }
    if (currentImageIndex < 0 || currentImageIndex >= static_cast<int>(siblingImages.size())) {
      currentImageIndex = 0;
    }
    std::string dirPath = FsHelpers::extractFolderPath(pathToDelete);
    if (dirPath.back() != '/') dirPath += "/";
    filePath = dirPath + siblingImages[static_cast<size_t>(currentImageIndex)];
    reloadView();
  };

  size_t lastSlash = pathToDelete.find_last_of('/');
  const std::string leaf =
      (lastSlash != std::string::npos) ? pathToDelete.substr(lastSlash + 1) : pathToDelete;
  const std::string heading = std::string(tr(STR_DELETE)) + "? ";
  startActivityForResult(std::make_unique<ConfirmationActivity>(renderer, mappedInput, heading, leaf, true), handler);
}

void BmpViewerActivity::onEnter() {
  Activity::onEnter();
  entryOrientation = renderer.getOrientation();
  screenUpToDate = false;
  displayedPath.clear();

  if (siblingImages.empty() && !filePath.empty()) {
    loadSiblingImages();
  }

  requestUpdate();
}

void BmpViewerActivity::reloadView() {
  screenUpToDate = false;
  requestUpdate();
}

void BmpViewerActivity::render(RenderLock&&) {
  if (screenUpToDate && displayedPath == filePath) {
    return;
  }

  renderer.clearScreen();
  esp_task_wdt_reset();
  const bool ok = renderImage();
  esp_task_wdt_reset();

  const bool hasPrevious = (siblingImages.size() > 1 && currentImageIndex > 0);
  const bool hasNext =
      (siblingImages.size() > 1 && currentImageIndex != -1 &&
       currentImageIndex < static_cast<int>(siblingImages.size()) - 1);

  if (ok) {
#if defined(BOARD_ESP32_S3_EPAPER_397)
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", (hasPrevious ? "<" : ""), (hasNext ? ">" : ""));
#else
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SET_SLEEP_COVER), (hasPrevious ? "<" : ""),
                                              (hasNext ? ">" : ""));
#endif
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    esp_task_wdt_reset();
    displayedPath = filePath;
    screenUpToDate = true;
    renderer.displayBuffer();
  } else {
    renderer.setOrientation(entryOrientation);
    renderer.clearScreen();
    renderer.drawCenteredText(UI_10_FONT_ID, renderer.getScreenHeight() / 2, tr(STR_MEMORY_ERROR));
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    esp_task_wdt_reset();
    renderer.displayBuffer();
  }
}

void BmpViewerActivity::onExit() {
  Activity::onExit();
  screenUpToDate = false;
  displayedPath.clear();
  renderer.setOrientation(entryOrientation);
  // Clear framebuffer only; next activity's render() will refresh the panel.
  // displayBuffer() here blocked the main thread ~3–4 s and raced with CPU throttling.
  renderer.clearScreen();
}

void BmpViewerActivity::doSetSleepCover() {
  GUI.drawPopup(renderer, tr(STR_LOADING_POPUP));

  bool success = false;
  FsFile inFile, outFile;
  if (Storage.openFileForRead("BMP", filePath, inFile)) {
    if (Storage.openFileForWrite("BMP", "/sleep.bmp", outFile)) {
      char buffer[2048];
      int bytesRead;
      success = true;
      while ((bytesRead = inFile.read(buffer, sizeof(buffer))) > 0) {
        if (outFile.write(buffer, bytesRead) != bytesRead) {
          success = false;
          break;
        }
      }
      outFile.close();
    }
    inFile.close();
  }

  if (success) {
    SETTINGS.sleepScreen = CrossPointSettings::SLEEP_SCREEN_MODE::CUSTOM;
    SETTINGS.saveToFile();
    GUI.drawPopup(renderer, tr(STR_DONE));
  } else {
    GUI.drawPopup(renderer, tr(STR_FAILED_LOWER));
  }

  delay(1000);
  requestUpdateAndWait();
}

void BmpViewerActivity::loop() {
  Activity::loop();

  if (mappedInput.wasBackClicked()) {
    if (activityManager.hasStackedActivities()) {
      finish();
    } else {
      activityManager.goHome();
    }
    return;
  }

#if defined(BOARD_ESP32_S3_EPAPER_397)
  if (consumeShakeDeleteRequest()) {
    doDeleteImage();
    return;
  }
#else
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    doSetSleepCover();
    return;
  }
#endif

  if (mappedInput.wasReleased(MappedInputManager::Button::Left) ||
      mappedInput.wasReleased(MappedInputManager::Button::Up)) {
    if (siblingImages.size() > 1 && currentImageIndex > 0) {
      currentImageIndex--;
      std::string dirPath = FsHelpers::extractFolderPath(filePath);
      if (dirPath.back() != '/') dirPath += "/";
      filePath = dirPath + siblingImages[static_cast<size_t>(currentImageIndex)];
      reloadView();
    }
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Right) ||
      mappedInput.wasReleased(MappedInputManager::Button::Down)) {
    if (siblingImages.size() > 1 && currentImageIndex != -1 &&
        currentImageIndex < static_cast<int>(siblingImages.size()) - 1) {
      currentImageIndex++;
      std::string dirPath = FsHelpers::extractFolderPath(filePath);
      if (dirPath.back() != '/') dirPath += "/";
      filePath = dirPath + siblingImages[static_cast<size_t>(currentImageIndex)];
      reloadView();
    }
    return;
  }
}
