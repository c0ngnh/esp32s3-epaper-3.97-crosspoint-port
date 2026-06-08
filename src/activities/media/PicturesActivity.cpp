#include "PicturesActivity.h"

#if defined(BOARD_ESP32_S3_EPAPER_397)

#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>

#include <cstring>

#include "activities/util/BmpViewerActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/UnifiedAppLayout.h"

namespace {
constexpr const char* PICTURES_DIR = "/pictures";

bool isImageLeaf(const std::string& name) {
  return FsHelpers::hasBmpExtension(name) || FsHelpers::hasPngExtension(name) || FsHelpers::hasJpgExtension(name);
}
}  // namespace

void PicturesActivity::loadImages() {
  images.clear();
  if (!Storage.exists(PICTURES_DIR)) {
    Storage.mkdir(PICTURES_DIR);
    return;
  }
  auto dir = Storage.open(PICTURES_DIR);
  if (!dir || !dir.isDirectory()) {
    if (dir) {
      dir.close();
    }
    return;
  }
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
      if (leaf[0] != '.' && isImageLeaf(leaf)) {
        images.push_back(leaf);
      }
    }
    file.close();
  }
  dir.close();
  FsHelpers::sortFileList(images);
}

void PicturesActivity::syncPreviewFromSelection() {
  previewPath.clear();
  if (selectorIndex < images.size()) {
    previewPath = std::string(PICTURES_DIR) + "/" + images[selectorIndex];
  }
}

void PicturesActivity::drawPreviewTile(const Rect& tile) const {
  if (!previewPath.empty() && UnifiedAppLayout::drawImageCoverInRoundedTile(renderer, previewPath, tile)) {
    return;
  }
  UnifiedAppLayout::drawTileSurface(renderer, tile);
  renderer.drawCenteredText(UI_12_FONT_ID, tile.y + tile.height / 2, tr(STR_NO_IMAGE_FILES));
}

void PicturesActivity::onEnter() {
  Activity::onEnter();
  loadImages();
  selectorIndex = 0;
  syncPreviewFromSelection();
  requestUpdate();
}

void PicturesActivity::onExit() { Activity::onExit(); }

void PicturesActivity::loop() {
  if (mappedInput.wasBackClicked()) {
    activityManager.goHome();
    return;
  }

  const int count = static_cast<int>(images.size());
  if (count > 0) {
    buttonNavigator.onNextRelease([this, count] {
      selectorIndex = static_cast<size_t>(ButtonNavigator::nextIndex(static_cast<int>(selectorIndex), count));
      syncPreviewFromSelection();
      requestUpdate();
    });
    buttonNavigator.onPreviousRelease([this, count] {
      selectorIndex = static_cast<size_t>(ButtonNavigator::previousIndex(static_cast<int>(selectorIndex), count));
      syncPreviewFromSelection();
      requestUpdate();
    });
  }

  if (mappedInput.wasConfirmClicked() && !images.empty() && selectorIndex < images.size()) {
    const std::string path = std::string(PICTURES_DIR) + "/" + images[selectorIndex];
    activityManager.pushActivity(std::make_unique<BmpViewerActivity>(renderer, mappedInput, path));
  }
}

void PicturesActivity::render(RenderLock&&) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  const int headerBottom = metrics.topPadding + metrics.headerHeight;

  renderer.clearScreen();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_PICTURES));

  const auto layout = UnifiedAppLayout::splitBelowHeader(renderer, headerBottom);
  drawPreviewTile(layout.bigTile);

  if (images.empty()) {
    renderer.drawText(UI_10_FONT_ID, metrics.contentSidePadding, layout.menu.y + 8, tr(STR_NO_IMAGE_FILES), true);
  } else {
    GUI.drawButtonMenu(renderer, layout.menu, static_cast<int>(images.size()), static_cast<int>(selectorIndex),
                       [this](int i) { return images[static_cast<size_t>(i)]; }, nullptr,
                       UnifiedAppLayout::kMenuVisibleRows);
  }

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_OPEN), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}

#endif
