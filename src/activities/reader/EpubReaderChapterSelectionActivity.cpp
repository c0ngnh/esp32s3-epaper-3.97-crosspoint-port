#include "EpubReaderChapterSelectionActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>
#include <string>

#include "MappedInputManager.h"
#include "ReaderUtils.h"
#include "components/UITheme.h"
#include "util/AppScreenLayout.h"

int EpubReaderChapterSelectionActivity::getTotalItems() const { return epub->getTocItemsCount(); }

void EpubReaderChapterSelectionActivity::onEnter() {
  Activity::onEnter();

  if (!epub) {
    ActivityResult result;
    result.isCancelled = true;
    setResult(std::move(result));
    finish();
    return;
  }

  selectorIndex = epub->getTocIndexForSpineIndex(currentSpineIndex);
  if (selectorIndex == -1) {
    selectorIndex = 0;
  }

  requestUpdate();
}

void EpubReaderChapterSelectionActivity::onExit() { Activity::onExit(); }

void EpubReaderChapterSelectionActivity::loop() {
  if (!epub) {
    return;
  }

  const int totalItems = getTotalItems();

  if (mappedInput.wasConfirmClicked()) {
    const auto newSpineIndex = epub->getSpineIndexForTocIndex(selectorIndex);
    if (newSpineIndex == -1) {
      ActivityResult result;
      result.isCancelled = true;
      setResult(std::move(result));
      finish();
    } else {
      setResult(ChapterResult{newSpineIndex});
      finish();
    }
  } else if (ReaderUtils::wasShortBackClicked(mappedInput)) {
    ActivityResult result;
    result.isCancelled = true;
    setResult(std::move(result));
    finish();
  }

  buttonNavigator.onNextRelease([this, totalItems] {
    selectorIndex = ButtonNavigator::nextIndex(selectorIndex, totalItems);
    requestUpdate();
  });

  buttonNavigator.onPreviousRelease([this, totalItems] {
    selectorIndex = ButtonNavigator::previousIndex(selectorIndex, totalItems);
    requestUpdate();
  });

  buttonNavigator.onNextContinuous([this, totalItems] {
    const auto screen = AppScreenLayout::listScreen(renderer, true);
    const auto& m = UITheme::getInstance().getMetrics();
    const int rowHeight = m.listRowHeight;
    const int pageItems = std::max(1, screen.body.height / rowHeight);
    selectorIndex = ButtonNavigator::nextPageIndex(selectorIndex, totalItems, pageItems);
    requestUpdate();
  });

  buttonNavigator.onPreviousContinuous([this, totalItems] {
    const auto screen = AppScreenLayout::listScreen(renderer, true);
    const auto& m = UITheme::getInstance().getMetrics();
    const int rowHeight = m.listRowHeight;
    const int pageItems = std::max(1, screen.body.height / rowHeight);
    selectorIndex = ButtonNavigator::previousPageIndex(selectorIndex, totalItems, pageItems);
    requestUpdate();
  });
}

void EpubReaderChapterSelectionActivity::render(RenderLock&&) {
  renderer.clearScreen();
  const auto screen = AppScreenLayout::listScreen(renderer, true);
  const int totalItems = getTotalItems();

  GUI.drawHeader(renderer, screen.header, tr(STR_SELECT_CHAPTER));

  if (totalItems <= 0) {
    AppScreenLayout::drawBodyMessage(renderer, screen.body, tr(STR_NO_CHAPTERS));
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer();
    return;
  }

  selectorIndex = std::clamp(selectorIndex, 0, totalItems - 1);

  GUI.drawList(renderer, screen.body, totalItems, selectorIndex,
               [this](int index) {
                 const auto item = epub->getTocItem(index);
                 std::string title = item.title;
                 if (item.level > 1) {
                   title.insert(0, static_cast<size_t>(item.level - 1) * 2, ' ');
                 }
                 return title;
               });

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}
