#include "EpubReaderFootnotesActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>

#include "MappedInputManager.h"
#include "ReaderUtils.h"
#include "components/UITheme.h"
#include "util/AppScreenLayout.h"

void EpubReaderFootnotesActivity::onEnter() {
  Activity::onEnter();
  selectedIndex = 0;
  requestUpdate();
}

void EpubReaderFootnotesActivity::onExit() { Activity::onExit(); }

void EpubReaderFootnotesActivity::loop() {
  if (ReaderUtils::wasShortBackClicked(mappedInput)) {
    ActivityResult result;
    result.isCancelled = true;
    setResult(std::move(result));
    finish();
    return;
  }

  if (mappedInput.wasConfirmClicked()) {
    if (footnotes.empty()) {
      ActivityResult result;
      result.isCancelled = true;
      setResult(std::move(result));
      finish();
      return;
    }
    if (selectedIndex >= 0 && selectedIndex < static_cast<int>(footnotes.size())) {
      setResult(FootnoteResult{footnotes[selectedIndex].href});
      finish();
    }
    return;
  }

  if (!footnotes.empty()) {
    buttonNavigator.onNextRelease([this] {
      selectedIndex = ButtonNavigator::nextIndex(selectedIndex, static_cast<int>(footnotes.size()));
      requestUpdate();
    });
    buttonNavigator.onPreviousRelease([this] {
      selectedIndex = ButtonNavigator::previousIndex(selectedIndex, static_cast<int>(footnotes.size()));
      requestUpdate();
    });
  }
}

void EpubReaderFootnotesActivity::render(RenderLock&&) {
  renderer.clearScreen();
  const auto screen = AppScreenLayout::listScreen(renderer, true);

  GUI.drawHeader(renderer, screen.header, tr(STR_FOOTNOTES));

  if (footnotes.empty()) {
    AppScreenLayout::drawBodyMessage(renderer, screen.body, tr(STR_NO_FOOTNOTES));
  } else {
    selectedIndex = std::clamp(selectedIndex, 0, static_cast<int>(footnotes.size()) - 1);
    GUI.drawList(renderer, screen.body, static_cast<int>(footnotes.size()), selectedIndex,
                 [this](int index) {
                   std::string label = footnotes[index].number;
                   if (label.empty()) {
                     label = tr(STR_LINK);
                   }
                   return label;
                 });
  }

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}
