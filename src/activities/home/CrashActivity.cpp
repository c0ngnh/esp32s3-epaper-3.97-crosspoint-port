#include "CrashActivity.h"

#include <GfxRenderer.h>
#include <HalStorage.h>
#include <HalSystem.h>
#include <I18n.h>

#include <cstdio>

#include "activities/ActivityManager.h"
#include "components/UITheme.h"
#include "fontIds.h"

void CrashActivity::onEnter() {
  Activity::onEnter();

  panicMessage = HalSystem::getPanicInfo(false);
  if (panicMessage.empty()) {
    panicMessage = tr(STR_CRASH_NO_REASON);
  }

  bootInfo_.clear();
  char line[128];
  snprintf(line, sizeof(line), "%s: %s", tr(STR_CRASH_RESET_REASON), HalSystem::getResetReasonString().c_str());
  bootInfo_ = line;
  bootInfo_ += "\n";
  snprintf(line, sizeof(line), "%s: %s", tr(STR_CRASH_WAKE_CAUSE), HalSystem::getWakeupCauseString().c_str());
  bootInfo_ += line;

  if (Storage.exists("/crash_report.txt")) {
    const String file = Storage.readFile("/crash_report.txt");
    if (!file.isEmpty() && file.length() < 800) {
      bootInfo_ += "\n\n";
      bootInfo_ += file.c_str();
    }
  }

  HalSystem::clearPanic();

  requestUpdateAndWait();
}

void CrashActivity::loop() {
#if defined(BOARD_ESP32_S3_EPAPER_397)
  // Tap BOOT (Back) or Select — not Select+BOOT hold (that is the global screenshot combo).
  if (mappedInput.wasBackClicked() || consumeConfirmClick()) {
#else
  if (mappedInput.wasPressed(MappedInputManager::Button::Back) ||
      mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
#endif
    activityManager.goHome();
  }
}

void CrashActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const auto contentWidth = pageWidth - 2 * metrics.contentSidePadding;
  const auto x = metrics.contentSidePadding;
  const auto lineHeight = renderer.getLineHeight(UI_10_FONT_ID);

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_CRASH_TITLE));

  int y = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;

  auto descLines = renderer.wrappedText(UI_10_FONT_ID, tr(STR_CRASH_DESCRIPTION), contentWidth, 6);
  for (const auto& line : descLines) {
    renderer.drawText(UI_10_FONT_ID, x, y, line.c_str());
    y += lineHeight;
  }

  y += metrics.verticalSpacing;
  auto bootLines = renderer.wrappedText(UI_10_FONT_ID, bootInfo_.c_str(), contentWidth, 8);
  for (const auto& line : bootLines) {
    renderer.drawText(UI_10_FONT_ID, x, y, line.c_str());
    y += lineHeight;
  }

  y += metrics.verticalSpacing;
  renderer.drawText(UI_10_FONT_ID, x, y, tr(STR_CRASH_REASON));
  y += lineHeight + metrics.verticalSpacing;

  auto panicLines = renderer.wrappedText(UI_10_FONT_ID, panicMessage.c_str(), contentWidth, 5);
  for (const auto& line : panicLines) {
    renderer.drawText(UI_10_FONT_ID, x, y, line.c_str());
    y += lineHeight;
  }

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_DONE), "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}
