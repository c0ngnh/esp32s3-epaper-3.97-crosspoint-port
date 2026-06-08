#include "XtcReaderChapterSelectionActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>

#include "MappedInputManager.h"
#include "ReaderUtils.h"
#include "components/UITheme.h"
#include "util/AppScreenLayout.h"

int XtcReaderChapterSelectionActivity::findChapterIndexForPage(uint32_t page) const {
  if (!xtc) {
    return 0;
  }

  const auto& chapters = xtc->getChapters();
  for (size_t i = 0; i < chapters.size(); i++) {
    if (page >= chapters[i].startPage && page <= chapters[i].endPage) {
      return static_cast<int>(i);
    }
  }
  return 0;
}

void XtcReaderChapterSelectionActivity::onEnter() {
  Activity::onEnter();

  if (!xtc) {
    ActivityResult result;
    result.isCancelled = true;
    setResult(std::move(result));
    finish();
    return;
  }

  selectorIndex = findChapterIndexForPage(currentPage);

  requestUpdate();
}

void XtcReaderChapterSelectionActivity::onExit() { Activity::onExit(); }

void XtcReaderChapterSelectionActivity::loop() {
  if (!xtc) {
    return;
  }

  const int totalItems = static_cast<int>(xtc->getChapters().size());

  if (mappedInput.wasConfirmClicked()) {
    const auto& chapters = xtc->getChapters();
    if (!chapters.empty() && selectorIndex >= 0 && selectorIndex < static_cast<int>(chapters.size())) {
      setResult(PageResult{chapters[selectorIndex].startPage});
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
    const int pageItems = std::max(1, screen.body.height / m.listRowHeight);
    selectorIndex = ButtonNavigator::nextPageIndex(selectorIndex, totalItems, pageItems);
    requestUpdate();
  });

  buttonNavigator.onPreviousContinuous([this, totalItems] {
    const auto screen = AppScreenLayout::listScreen(renderer, true);
    const auto& m = UITheme::getInstance().getMetrics();
    const int pageItems = std::max(1, screen.body.height / m.listRowHeight);
    selectorIndex = ButtonNavigator::previousPageIndex(selectorIndex, totalItems, pageItems);
    requestUpdate();
  });
}

void XtcReaderChapterSelectionActivity::render(RenderLock&&) {
  renderer.clearScreen();
  const auto screen = AppScreenLayout::listScreen(renderer, true);
  const auto& chapters = xtc->getChapters();
  const int totalItems = static_cast<int>(chapters.size());

  GUI.drawHeader(renderer, screen.header, tr(STR_SELECT_CHAPTER));

  if (chapters.empty()) {
    AppScreenLayout::drawBodyMessage(renderer, screen.body, tr(STR_NO_CHAPTERS));
  } else {
    selectorIndex = std::clamp(selectorIndex, 0, totalItems - 1);
    GUI.drawList(renderer, screen.body, totalItems, selectorIndex, [&](int index) {
      const auto& chapter = chapters[index];
      return chapter.name.empty() ? std::string(tr(STR_UNNAMED)) : chapter.name;
    });
  }

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}
