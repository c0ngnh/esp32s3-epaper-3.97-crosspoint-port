#include "SleepActivity.h"

#include <Epub.h>
#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>
#include <Txt.h>
#include <Xtc.h>

#if defined(BOARD_ESP32_S3_EPAPER_397)
#include <JpegToFramebufferConverter.h>
#include <PngToFramebufferConverter.h>
#endif

#include "CrossPointSettings.h"
#include "CrossPointState.h"
#include "activities/reader/ReaderUtils.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "images/Logo120.h"
#include "util/ImageFitUtil.h"

#if defined(BOARD_ESP32_S3_EPAPER_397)
namespace {

constexpr const char* PICTURES_DIR = "/pictures";

bool isSleepImageFile(const std::string& name) {
  return FsHelpers::hasBmpExtension(name) || FsHelpers::hasPngExtension(name) ||
         FsHelpers::hasJpgExtension(name);
}

}  // namespace
#endif

void SleepActivity::onEnter() {
  Activity::onEnter();

#if defined(BOARD_ESP32_S3_EPAPER_397)
  if (render397PicturesSleepScreen(_shutdownWallpaper)) {
    return;
  }
#endif

  // Show popup with reader orientation only when going to sleep from reader
  if (APP_STATE.lastSleepFromReader) {
    ReaderUtils::applyOrientation(renderer, SETTINGS.orientation);
    GUI.drawPopup(renderer, tr(STR_ENTERING_SLEEP));
    renderer.setOrientation(GfxRenderer::Orientation::Portrait);
  } else {
    GUI.drawPopup(renderer, tr(STR_ENTERING_SLEEP));
  }

  switch (SETTINGS.sleepScreen) {
    case (CrossPointSettings::SLEEP_SCREEN_MODE::BLANK):
      return renderBlankSleepScreen();
    case (CrossPointSettings::SLEEP_SCREEN_MODE::CUSTOM):
      return renderCustomSleepScreen();
    case (CrossPointSettings::SLEEP_SCREEN_MODE::COVER):
      return renderCoverSleepScreen();
    case (CrossPointSettings::SLEEP_SCREEN_MODE::COVER_CUSTOM):
      if (APP_STATE.lastSleepFromReader) {
        return renderCoverSleepScreen();
      } else {
        return renderCustomSleepScreen();
      }
    default:
      return renderDefaultSleepScreen();
  }
}

void SleepActivity::renderCustomSleepScreen() const {
  // Check if we have a /.sleep (preferred) or /sleep directory
  const char* sleepDir = nullptr;
  auto dir = Storage.open("/.sleep");

  // Look for sleep.bmp on the root of the sd card to determine if we should
  // render a custom sleep screen instead of the default.
  // This takes priority over the /sleep folder.
  FsFile file;
  if (Storage.openFileForRead("SLP", "/sleep.bmp", file)) {
    Bitmap bitmap(file, true);
    if (bitmap.parseHeaders() == BmpReaderError::Ok) {
      LOG_DBG("SLP", "Loading: /sleep.bmp");
      renderBitmapSleepScreen(bitmap);
      file.close();
      if (dir) dir.close();
      return;
    }
    file.close();
  }

  if (dir && dir.isDirectory()) {
    sleepDir = "/.sleep";
  } else {
    dir = Storage.open("/sleep");
    if (dir && dir.isDirectory()) {
      sleepDir = "/sleep";
    }
  }

  if (sleepDir) {
    std::vector<std::string> files;
    char name[500];
    // collect all valid BMP files
    for (auto dirFile = dir.openNextFile(); dirFile; dirFile = dir.openNextFile()) {
      if (dirFile.isDirectory()) {
        dirFile.close();
        continue;
      }
      dirFile.getName(name, sizeof(name));
      auto filename = std::string(name);
      if (filename[0] == '.') {
        dirFile.close();
        continue;
      }

      if (!FsHelpers::hasBmpExtension(filename)) {
        LOG_DBG("SLP", "Skipping non-.bmp file name: %s", name);
        dirFile.close();
        continue;
      }
      Bitmap bitmap(dirFile);
      if (bitmap.parseHeaders() != BmpReaderError::Ok) {
        LOG_DBG("SLP", "Skipping invalid BMP file: %s", name);
        dirFile.close();
        continue;
      }
      files.emplace_back(filename);
      dirFile.close();
    }
    const auto numFiles = files.size();
    if (numFiles > 0) {
      // Pick a random wallpaper, excluding recently shown ones.
      // Window: up to SLEEP_RECENT_COUNT entries, capped at numFiles-1.
      const uint16_t fileCount = static_cast<uint16_t>(std::min(numFiles, static_cast<size_t>(UINT16_MAX)));
      const uint8_t window =
          static_cast<uint8_t>(std::min(static_cast<size_t>(APP_STATE.recentSleepFill), numFiles - 1));
      auto randomFileIndex = static_cast<uint16_t>(random(fileCount));
      for (uint8_t attempt = 0; attempt < 20 && APP_STATE.isRecentSleep(randomFileIndex, window); attempt++) {
        randomFileIndex = static_cast<uint16_t>(random(fileCount));
      }
      APP_STATE.pushRecentSleep(randomFileIndex);
      APP_STATE.saveToFile();
      const auto filename = std::string(sleepDir) + "/" + files[randomFileIndex];
      FsFile randFile;
      if (Storage.openFileForRead("SLP", filename, randFile)) {
        LOG_DBG("SLP", "Randomly loading: %s/%s", sleepDir, files[randomFileIndex].c_str());
        delay(100);
        Bitmap bitmap(randFile, true);
        if (bitmap.parseHeaders() == BmpReaderError::Ok) {
          renderBitmapSleepScreen(bitmap);
          randFile.close();
          dir.close();
          return;
        }
        randFile.close();
      }
    }
  }
  if (dir) dir.close();

  renderDefaultSleepScreen();
}

void SleepActivity::drawSleepStatusIndicator() const {
  const char* label = _shutdownWallpaper ? tr(STR_POWERED_OFF) : tr(STR_SLEEPING);
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int padX = metrics.contentSidePadding;
  const int padBottom = SETTINGS.screenMargin + 10;
  const int pageHeight = renderer.getScreenHeight();
  constexpr int fontId = UI_12_FONT_ID;
  const int y = pageHeight - padBottom - renderer.getTextHeight(fontId);
  renderer.drawText(fontId, padX, y, label, true, EpdFontFamily::BOLD);
}

void SleepActivity::renderDefaultSleepScreen() const {
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  renderer.clearScreen();
  renderer.drawImage(Logo120, (pageWidth - 120) / 2, (pageHeight - 120) / 2, 120, 120);
  renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 + 70, tr(STR_CROSSPOINT), true, EpdFontFamily::BOLD);
  renderer.drawCenteredText(SMALL_FONT_ID, pageHeight / 2 + 95, tr(STR_SLEEPING));

  // SSD1677: framebuffer polarity vs X4 differs; avoid global invert — it flips wallpapers and assets wrong.
#if !defined(BOARD_ESP32_S3_EPAPER_397)
  if (SETTINGS.sleepScreen != CrossPointSettings::SLEEP_SCREEN_MODE::LIGHT) {
    renderer.invertScreen();
  }
#endif

  renderer.displayBuffer();
}

void SleepActivity::renderBitmapSleepScreen(const Bitmap& bitmap) const {
  const int imgW = bitmap.getWidth();
  const int imgH = bitmap.getHeight();
  const ImageFit bestFit = pickBestImageFit(renderer, imgW, imgH);
  const bool useFitMode = SETTINGS.sleepScreenCoverMode == CrossPointSettings::SLEEP_SCREEN_COVER_MODE::FIT;

  if (useFitMode) {
    const auto pageWidth = renderer.getScreenWidth();
    const auto pageHeight = renderer.getScreenHeight();
    LOG_DBG("SLP", "bitmap %d x %d fit %d x %d at %d,%d orient %d", imgW, imgH, bestFit.drawW, bestFit.drawH,
            bestFit.x, bestFit.y, static_cast<int>(bestFit.orientation));

    renderer.clearScreen();
    const bool hasGreyscale = bitmap.hasGreyscale() &&
                              SETTINGS.sleepScreenCoverFilter ==
                                  CrossPointSettings::SLEEP_SCREEN_COVER_FILTER::NO_FILTER;
    renderer.drawBitmap(bitmap, bestFit.x, bestFit.y, pageWidth, pageHeight, 0, 0);

    if (SETTINGS.sleepScreenCoverFilter == CrossPointSettings::SLEEP_SCREEN_COVER_FILTER::INVERTED_BLACK_AND_WHITE) {
      renderer.invertScreen();
    }

    drawSleepStatusIndicator();
    renderer.displayBuffer();

    if (hasGreyscale) {
      bitmap.rewindToData();
      renderer.clearScreen(0x00);
      renderer.setRenderMode(GfxRenderer::GRAYSCALE_LSB);
      renderer.drawBitmap(bitmap, bestFit.x, bestFit.y, pageWidth, pageHeight, 0, 0);
      renderer.copyGrayscaleLsbBuffers();

      bitmap.rewindToData();
      renderer.clearScreen(0x00);
      renderer.setRenderMode(GfxRenderer::GRAYSCALE_MSB);
      renderer.drawBitmap(bitmap, bestFit.x, bestFit.y, pageWidth, pageHeight, 0, 0);
      renderer.copyGrayscaleMsbBuffers();

      renderer.displayGrayBuffer();
      renderer.setRenderMode(GfxRenderer::BW);
    }
    return;
  }

  int x, y;
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  float cropX = 0, cropY = 0;

  LOG_DBG("SLP", "bitmap %d x %d, screen %d x %d (crop)", imgW, imgH, pageWidth, pageHeight);
  if (imgW > pageWidth || imgH > pageHeight) {
    // image will scale, make sure placement is right
    float ratio = static_cast<float>(imgW) / static_cast<float>(imgH);
    const float screenRatio = static_cast<float>(pageWidth) / static_cast<float>(pageHeight);

    LOG_DBG("SLP", "bitmap ratio: %f, screen ratio: %f", ratio, screenRatio);
    if (ratio > screenRatio) {
      // image wider than viewport ratio, scaled down image needs to be centered vertically
      if (SETTINGS.sleepScreenCoverMode == CrossPointSettings::SLEEP_SCREEN_COVER_MODE::CROP) {
        cropX = 1.0f - (screenRatio / ratio);
        LOG_DBG("SLP", "Cropping bitmap x: %f", cropX);
        ratio = (1.0f - cropX) * static_cast<float>(imgW) / static_cast<float>(imgH);
      }
      x = 0;
      y = std::round((static_cast<float>(pageHeight) - static_cast<float>(pageWidth) / ratio) / 2);
      LOG_DBG("SLP", "Centering with ratio %f to y=%d", ratio, y);
    } else {
      // image taller than viewport ratio, scaled down image needs to be centered horizontally
      if (SETTINGS.sleepScreenCoverMode == CrossPointSettings::SLEEP_SCREEN_COVER_MODE::CROP) {
        cropY = 1.0f - (ratio / screenRatio);
        LOG_DBG("SLP", "Cropping bitmap y: %f", cropY);
        ratio = static_cast<float>(imgW) / ((1.0f - cropY) * static_cast<float>(imgH));
      }
      x = std::round((static_cast<float>(pageWidth) - static_cast<float>(pageHeight) * ratio) / 2);
      y = 0;
      LOG_DBG("SLP", "Centering with ratio %f to x=%d", ratio, x);
    }
  } else {
    // center the image
    x = (pageWidth - imgW) / 2;
    y = (pageHeight - imgH) / 2;
  }

  LOG_DBG("SLP", "drawing to %d x %d", x, y);
  renderer.clearScreen();

  const bool hasGreyscale = bitmap.hasGreyscale() &&
                            SETTINGS.sleepScreenCoverFilter == CrossPointSettings::SLEEP_SCREEN_COVER_FILTER::NO_FILTER;

  renderer.drawBitmap(bitmap, x, y, pageWidth, pageHeight, cropX, cropY);

  if (SETTINGS.sleepScreenCoverFilter == CrossPointSettings::SLEEP_SCREEN_COVER_FILTER::INVERTED_BLACK_AND_WHITE) {
    renderer.invertScreen();
  }

  drawSleepStatusIndicator();
  renderer.displayBuffer();

  if (hasGreyscale) {
    bitmap.rewindToData();
    renderer.clearScreen(0x00);
    renderer.setRenderMode(GfxRenderer::GRAYSCALE_LSB);
    renderer.drawBitmap(bitmap, x, y, pageWidth, pageHeight, cropX, cropY);
    renderer.copyGrayscaleLsbBuffers();

    bitmap.rewindToData();
    renderer.clearScreen(0x00);
    renderer.setRenderMode(GfxRenderer::GRAYSCALE_MSB);
    renderer.drawBitmap(bitmap, x, y, pageWidth, pageHeight, cropX, cropY);
    renderer.copyGrayscaleMsbBuffers();

    renderer.displayGrayBuffer();
    renderer.setRenderMode(GfxRenderer::BW);
  }
}

void SleepActivity::renderCoverSleepScreen() const {
  void (SleepActivity::*renderNoCoverSleepScreen)() const;
  switch (SETTINGS.sleepScreen) {
    case (CrossPointSettings::SLEEP_SCREEN_MODE::COVER_CUSTOM):
      renderNoCoverSleepScreen = &SleepActivity::renderCustomSleepScreen;
      break;
    default:
      renderNoCoverSleepScreen = &SleepActivity::renderDefaultSleepScreen;
      break;
  }

  if (APP_STATE.openEpubPath.empty()) {
    return (this->*renderNoCoverSleepScreen)();
  }

  std::string coverBmpPath;
  bool cropped = SETTINGS.sleepScreenCoverMode == CrossPointSettings::SLEEP_SCREEN_COVER_MODE::CROP;

  // Check if the current book is XTC, TXT, or EPUB
  if (FsHelpers::hasXtcExtension(APP_STATE.openEpubPath)) {
    // Handle XTC file
    Xtc lastXtc(APP_STATE.openEpubPath, "/.crosspoint");
    if (!lastXtc.load()) {
      LOG_ERR("SLP", "Failed to load last XTC");
      return (this->*renderNoCoverSleepScreen)();
    }

    if (!lastXtc.generateCoverBmp()) {
      LOG_ERR("SLP", "Failed to generate XTC cover bmp");
      return (this->*renderNoCoverSleepScreen)();
    }

    coverBmpPath = lastXtc.getCoverBmpPath();
  } else if (FsHelpers::hasTxtExtension(APP_STATE.openEpubPath)) {
    // Handle TXT file - looks for cover image in the same folder
    Txt lastTxt(APP_STATE.openEpubPath, "/.crosspoint");
    if (!lastTxt.load()) {
      LOG_ERR("SLP", "Failed to load last TXT");
      return (this->*renderNoCoverSleepScreen)();
    }

    if (!lastTxt.generateCoverBmp()) {
      LOG_ERR("SLP", "No cover image found for TXT file");
      return (this->*renderNoCoverSleepScreen)();
    }

    coverBmpPath = lastTxt.getCoverBmpPath();
  } else if (FsHelpers::hasEpubExtension(APP_STATE.openEpubPath)) {
    // Handle EPUB file
    Epub lastEpub(APP_STATE.openEpubPath, "/.crosspoint");
    // Skip loading css since we only need metadata here
    if (!lastEpub.load(true, true)) {
      LOG_ERR("SLP", "Failed to load last epub");
      return (this->*renderNoCoverSleepScreen)();
    }

    if (!lastEpub.generateCoverBmp(cropped)) {
      LOG_ERR("SLP", "Failed to generate cover bmp");
      return (this->*renderNoCoverSleepScreen)();
    }

    coverBmpPath = lastEpub.getCoverBmpPath(cropped);
  } else {
    return (this->*renderNoCoverSleepScreen)();
  }

  FsFile file;
  if (Storage.openFileForRead("SLP", coverBmpPath, file)) {
    Bitmap bitmap(file);
    if (bitmap.parseHeaders() == BmpReaderError::Ok) {
      LOG_DBG("SLP", "Rendering sleep cover: %s", coverBmpPath.c_str());
      renderBitmapSleepScreen(bitmap);
      return;
    }
  }

  return (this->*renderNoCoverSleepScreen)();
}

void SleepActivity::renderBlankSleepScreen() const {
  renderer.clearScreen();
  drawSleepStatusIndicator();
  renderer.displayBuffer();
}

#if defined(BOARD_ESP32_S3_EPAPER_397)

bool SleepActivity::renderImageFileSleepScreen(const std::string& path) const {
  if (FsHelpers::hasBmpExtension(path)) {
    FsFile file;
    if (!Storage.openFileForRead("SLP", path, file)) {
      return false;
    }
    Bitmap bitmap(file, true);
    if (bitmap.parseHeaders() != BmpReaderError::Ok) {
      return false;
    }
    LOG_DBG("SLP", "Sleep image (bmp): %s", path.c_str());
    renderBitmapSleepScreen(bitmap);
    return true;
  }

  if (!FsHelpers::hasPngExtension(path) && !FsHelpers::hasJpgExtension(path)) {
    return false;
  }

  ImageDimensions dims{};
  if (FsHelpers::hasPngExtension(path)) {
    if (!PngToFramebufferConverter::getDimensionsStatic(path, dims)) {
      return false;
    }
  } else if (!JpegToFramebufferConverter::getDimensionsStatic(path, dims)) {
    return false;
  }

  const ImageFit fit = pickBestImageFit(renderer, dims.width, dims.height);
  LOG_DBG("SLP", "Sleep image orient %d fit %dx%d at %d,%d", static_cast<int>(fit.orientation), fit.drawW, fit.drawH,
          fit.x, fit.y);

  RenderConfig config{};
  config.x = fit.x;
  config.y = fit.y;
  config.maxWidth = fit.drawW;
  config.maxHeight = fit.drawH;
  config.useGrayscale = true;
  config.useDithering = true;
  config.useExactDimensions = true;

  renderer.clearScreen();
  bool ok = false;
  if (FsHelpers::hasPngExtension(path)) {
    PngToFramebufferConverter png;
    ok = png.decodeToFramebuffer(path, renderer, config);
  } else {
    JpegToFramebufferConverter jpeg;
    ok = jpeg.decodeToFramebuffer(path, renderer, config);
  }
  if (!ok) {
    return false;
  }

  drawSleepStatusIndicator();
#if !defined(BOARD_ESP32_S3_EPAPER_397)
  if (SETTINGS.sleepScreen != CrossPointSettings::SLEEP_SCREEN_MODE::LIGHT) {
    renderer.invertScreen();
  }
#endif
  LOG_DBG("SLP", "Sleep image: %s", path.c_str());
  renderer.displayBuffer();
  return true;
}

bool SleepActivity::render397PicturesSleepScreen(const bool shutdownWallpaper) const {
  if (!Storage.exists(PICTURES_DIR)) {
    Storage.mkdir(PICTURES_DIR);
  }

  const char* bgBasename = shutdownWallpaper ? "shutdown_bg" : "sleep_bg";
  static const char* kExts[] = {".bmp", ".png", ".jpg", ".jpeg"};
  for (const char* ext : kExts) {
    const std::string path = std::string(PICTURES_DIR) + "/" + bgBasename + ext;
    if (Storage.exists(path.c_str()) && renderImageFileSleepScreen(path)) {
      return true;
    }
  }

  auto dir = Storage.open(PICTURES_DIR);
  if (!dir || !dir.isDirectory()) {
    if (dir) {
      dir.close();
    }
    return false;
  }

  std::vector<std::string> images;
  char name[256];
  while (true) {
    auto file = dir.openNextFile();
    if (!file) {
      break;
    }
    if (!file.isDirectory()) {
      file.getName(name, sizeof(name));
      std::string leaf(name);
      const auto slash = leaf.find_last_of('/');
      if (slash != std::string::npos) {
        leaf = leaf.substr(slash + 1);
      }
      if (leaf[0] != '.' && isSleepImageFile(leaf)) {
        images.push_back(leaf);
      }
    }
    file.close();
  }
  dir.close();

  if (images.empty()) {
    return false;
  }

  FsHelpers::sortFileList(images);
  const size_t idx = static_cast<size_t>(random(static_cast<long>(images.size())));
  const std::string path = std::string(PICTURES_DIR) + "/" + images[idx];
  LOG_DBG("SLP", "Random sleep image: %s", path.c_str());
  return renderImageFileSleepScreen(path);
}

#endif
