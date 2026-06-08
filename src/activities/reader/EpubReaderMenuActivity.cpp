#include "EpubReaderMenuActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>

#include "MappedInputManager.h"
#include "ReaderUtils.h"
#include "components/UITheme.h"
#include "util/AppScreenLayout.h"

EpubReaderMenuActivity::EpubReaderMenuActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                               const std::string& title, const int currentPage, const int totalPages,
                                               const int bookProgressPercent, const uint8_t currentOrientation,
                                               const uint8_t currentPageTurnOption, const bool hasFootnotes)
    : Activity("EpubReaderMenu", renderer, mappedInput),
      menuItems(buildMenuItems(hasFootnotes)),
      title(title),
      pendingOrientation(currentOrientation % kOrientationOptionCount),
      selectedPageTurnOption(currentPageTurnOption % kPageTurnOptionCount),
      currentPage(currentPage),
      totalPages(totalPages),
      bookProgressPercent(bookProgressPercent) {}

std::vector<EpubReaderMenuActivity::MenuItem> EpubReaderMenuActivity::buildMenuItems(bool hasFootnotes) {
  std::vector<MenuItem> items;
  items.reserve(11);
  // Navigate
  items.push_back({MenuAction::SELECT_CHAPTER, StrId::STR_SELECT_CHAPTER});
  items.push_back({MenuAction::GO_TO_PERCENT, StrId::STR_GO_TO_PERCENT});
  items.push_back({MenuAction::BOOKMARKS, StrId::STR_BOOKMARKS});
  if (hasFootnotes) {
    items.push_back({MenuAction::FOOTNOTES, StrId::STR_FOOTNOTES});
  }
  // Reading options (toggle in place)
  items.push_back({MenuAction::ROTATE_SCREEN, StrId::STR_ORIENTATION});
  items.push_back({MenuAction::AUTO_PAGE_TURN, StrId::STR_AUTO_TURN_PAGES_PER_MIN});
  // Tools
  items.push_back({MenuAction::SCREENSHOT, StrId::STR_SCREENSHOT_BUTTON});
  items.push_back({MenuAction::DISPLAY_QR, StrId::STR_DISPLAY_QR});
  items.push_back({MenuAction::SYNC, StrId::STR_SYNC_PROGRESS});
  // Leave / maintenance
  items.push_back({MenuAction::GO_HOME, StrId::STR_GO_HOME_BUTTON});
  items.push_back({MenuAction::DELETE_CACHE, StrId::STR_DELETE_CACHE});
  return items;
}

std::string EpubReaderMenuActivity::menuRowTitle(const int index) const {
  if (index < 0 || index >= static_cast<int>(menuItems.size())) {
    return {};
  }
  return I18N.get(menuItems[index].labelId);
}

std::string EpubReaderMenuActivity::menuRowValue(const int index) const {
  if (index < 0 || index >= static_cast<int>(menuItems.size())) {
    return {};
  }
  switch (menuItems[index].action) {
    case MenuAction::ROTATE_SCREEN:
      return I18N.get(orientationLabels[pendingOrientation % orientationLabels.size()]);
    case MenuAction::AUTO_PAGE_TURN:
      return I18N.get(pageTurnLabels[selectedPageTurnOption % pageTurnLabels.size()]);
    default:
      return {};
  }
}

std::string EpubReaderMenuActivity::progressSubtitle() const {
  char buf[48];
  if (totalPages > 0) {
    snprintf(buf, sizeof(buf), "%d/%d  %d%%", currentPage, totalPages, bookProgressPercent);
  } else {
    snprintf(buf, sizeof(buf), "%d%%", bookProgressPercent);
  }
  return buf;
}

void EpubReaderMenuActivity::onEnter() {
  Activity::onEnter();
  requestUpdate();
}

void EpubReaderMenuActivity::onExit() { Activity::onExit(); }

void EpubReaderMenuActivity::loop() {
  // Handle navigation
  buttonNavigator.onNext([this] {
    selectedIndex = ButtonNavigator::nextIndex(selectedIndex, static_cast<int>(menuItems.size()));
    requestUpdate();
  });

  buttonNavigator.onPrevious([this] {
    selectedIndex = ButtonNavigator::previousIndex(selectedIndex, static_cast<int>(menuItems.size()));
    requestUpdate();
  });

  if (mappedInput.wasConfirmClicked()) {
    const auto selectedAction = menuItems[selectedIndex].action;
    if (selectedAction == MenuAction::ROTATE_SCREEN) {
      // Cycle orientation preview locally; actual rotation happens on menu exit.
      pendingOrientation = static_cast<uint8_t>((pendingOrientation + 1) % orientationLabels.size());
      requestUpdate();
      return;
    }

    if (selectedAction == MenuAction::AUTO_PAGE_TURN) {
      selectedPageTurnOption = static_cast<uint8_t>((selectedPageTurnOption + 1) % pageTurnLabels.size());
      requestUpdate();
      return;
    }

    setResult(MenuResult{static_cast<int>(selectedAction), pendingOrientation, selectedPageTurnOption});
    finish();
    return;
  } else if (ReaderUtils::wasShortBackClicked(mappedInput)) {
    ActivityResult result;
    result.isCancelled = true;
    result.data = MenuResult{-1, pendingOrientation, selectedPageTurnOption};
    setResult(std::move(result));
    finish();
    return;
  }
}

void EpubReaderMenuActivity::render(RenderLock&&) {
  renderer.clearScreen();
  const auto screen = AppScreenLayout::listScreen(renderer, true);
  const std::string subtitle = progressSubtitle();

  GUI.drawHeader(renderer, screen.header, title.c_str(), subtitle.c_str());

  const int count = static_cast<int>(menuItems.size());
  selectedIndex = std::clamp(selectedIndex, 0, std::max(0, count - 1));

  GUI.drawList(renderer, screen.body, count, selectedIndex,
               [this](int index) { return menuRowTitle(index); }, nullptr, nullptr,
               [this](int index) { return menuRowValue(index); }, true);

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}
