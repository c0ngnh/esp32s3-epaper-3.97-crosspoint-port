#include "EpubBookmarksActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>
#include <cstdio>

#include "MappedInputManager.h"
#include "activities/reader/ReaderUtils.h"
#include "components/UITheme.h"
#include "fontIds.h"

EpubBookmarksActivity::EpubBookmarksActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                             std::shared_ptr<Epub> epubIn, std::string bookPathIn)
    : Activity("EpubBookmarks", renderer, mappedInput), epub(std::move(epubIn)), bookPath(std::move(bookPathIn)) {}

namespace {

void finishCancelled(Activity& activity) {
  ActivityResult result;
  result.isCancelled = true;
  activity.setResult(std::move(result));
  activity.finish();
}

}  // namespace

void EpubBookmarksActivity::onEnter() {
  Activity::onEnter();
  BOOKMARK_STORE.loadFromFile();
  entries = BOOKMARK_STORE.forBook(bookPath);
  selectorIndex = 0;
  requestUpdate();
}

void EpubBookmarksActivity::loop() {
  if (entries.empty()) {
    if (ReaderUtils::wasShortBackClicked(mappedInput)) {
      finishCancelled(*this);
    }
    return;
  }

  buttonNavigator.onNext([this] {
    selectorIndex = ButtonNavigator::nextIndex(selectorIndex, static_cast<int>(entries.size()));
    selectorIndex = std::clamp(selectorIndex, 0, static_cast<int>(entries.size()) - 1);
    requestUpdate();
  });
  buttonNavigator.onPrevious([this] {
    selectorIndex = ButtonNavigator::previousIndex(selectorIndex, static_cast<int>(entries.size()));
    selectorIndex = std::clamp(selectorIndex, 0, static_cast<int>(entries.size()) - 1);
    requestUpdate();
  });

  if (ReaderUtils::wasShortBackClicked(mappedInput)) {
    finishCancelled(*this);
    return;
  }

  if (mappedInput.wasConfirmClicked()) {
    const auto& b = entries[selectorIndex];
    setResult(BookmarkResult{b.spineIndex, b.pageIndex});
    finish();
  }
}

void EpubBookmarksActivity::render(RenderLock&&) {
  renderer.clearScreen();
  const auto& m = UITheme::getInstance().getMetrics();
  const int w = renderer.getScreenWidth();
  GUI.drawHeader(renderer, Rect{0, m.topPadding, w, m.headerHeight}, tr(STR_BOOKMARKS));

  const int top = m.topPadding + m.headerHeight + m.verticalSpacing;
  if (entries.empty()) {
    renderer.drawText(UI_10_FONT_ID, m.contentSidePadding, top + 40, tr(STR_NO_BOOKMARKS), true);
  } else {
    selectorIndex = std::max(0, std::min(selectorIndex, static_cast<int>(entries.size()) - 1));
    GUI.drawList(renderer, Rect{0, top, w, renderer.getScreenHeight() - top - m.buttonHintsHeight},
                 static_cast<int>(entries.size()), selectorIndex,
                 [&](int i) {
                   char buf[32];
                   snprintf(buf, sizeof(buf), "Ch %d p%d (%d%%)", entries[i].spineIndex + 1, entries[i].pageIndex + 1,
                            entries[i].percent);
                   return std::string(buf);
                 },
                 [&](int i) {
                   char buf[24];
                   snprintf(buf, sizeof(buf), "%lu", static_cast<unsigned long>(entries[i].createdMs));
                   return std::string(buf);
                 });
  }

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), entries.empty() ? "" : tr(STR_GO_TO), tr(STR_DIR_UP),
                                            tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}
