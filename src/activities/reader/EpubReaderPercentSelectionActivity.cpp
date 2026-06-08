#include "EpubReaderPercentSelectionActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <cstdio>

#include "MappedInputManager.h"
#include "ReaderUtils.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/AppScreenLayout.h"

namespace {
constexpr int kSmallStep = 1;
constexpr int kLargeStep = 10;
}  // namespace

void EpubReaderPercentSelectionActivity::onEnter() {
  Activity::onEnter();
  requestUpdate();
}

void EpubReaderPercentSelectionActivity::onExit() { Activity::onExit(); }

void EpubReaderPercentSelectionActivity::adjustPercent(const int delta) {
  percent += delta;
  if (percent < 0) {
    percent = 0;
  } else if (percent > 100) {
    percent = 100;
  }
  requestUpdate();
}

void EpubReaderPercentSelectionActivity::loop() {
  if (ReaderUtils::wasShortBackClicked(mappedInput)) {
    ActivityResult result;
    result.isCancelled = true;
    setResult(std::move(result));
    finish();
    return;
  }

  if (mappedInput.wasConfirmClicked()) {
    setResult(PercentResult{percent});
    finish();
    return;
  }

  buttonNavigator.onPressAndContinuous({MappedInputManager::Button::Left}, [this] { adjustPercent(-kSmallStep); });
  buttonNavigator.onPressAndContinuous({MappedInputManager::Button::Right}, [this] { adjustPercent(kSmallStep); });

  buttonNavigator.onPressAndContinuous({MappedInputManager::Button::Up}, [this] { adjustPercent(kLargeStep); });
  buttonNavigator.onPressAndContinuous({MappedInputManager::Button::Down}, [this] { adjustPercent(-kLargeStep); });
}

void EpubReaderPercentSelectionActivity::render(RenderLock&&) {
  renderer.clearScreen();
  const auto screen = AppScreenLayout::listScreen(renderer, true);
  const auto& m = UITheme::getInstance().getMetrics();

  GUI.drawHeader(renderer, screen.header, tr(STR_GO_TO_PERCENT));

  char percentText[16];
  snprintf(percentText, sizeof(percentText), "%d%%", percent);
  const int valueLineH = renderer.getLineHeight(UI_12_FONT_ID);
  const int valueY = screen.body.y + screen.body.height / 4;
  renderer.drawCenteredText(UI_12_FONT_ID, valueY, percentText, true, EpdFontFamily::BOLD);

  const int barMargin = m.contentSidePadding + 12;
  const int barWidth = screen.body.width - barMargin * 2;
  const int barHeight = m.progressBarHeight;
  const int barX = screen.body.x + (screen.body.width - barWidth) / 2;
  const int barY = valueY + valueLineH + m.verticalSpacing * 2;

  renderer.drawRect(barX, barY, barWidth, barHeight, true);
  const int fillWidth = (barWidth - 4) * percent / 100;
  if (fillWidth > 0) {
    renderer.fillRect(barX + 2, barY + 2, fillWidth, barHeight - 4, true);
  }

  const int hintY = barY + barHeight + m.verticalSpacing;
  renderer.drawCenteredText(SMALL_FONT_ID, hintY, tr(STR_PERCENT_STEP_HINT), true);

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), "-", "+");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}
