#include "FullScreenMessageActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <vector>

#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/AppScreenLayout.h"

#if defined(BOARD_ESP32_S3_EPAPER_397)
#include <AlarmSound397.h>
#include <AudioFilePlayer.h>
#endif

namespace {
std::vector<std::string> splitLines(const std::string& text) {
  std::vector<std::string> lines;
  size_t start = 0;
  while (start < text.size()) {
    size_t end = text.find('\n', start);
    if (end == std::string::npos) {
      end = text.size();
    }
    lines.push_back(text.substr(start, end - start));
    start = end + 1;
  }
  if (lines.empty()) {
    lines.emplace_back("");
  }
  return lines;
}
}  // namespace

void FullScreenMessageActivity::onEnter() {
  Activity::onEnter();
  requestUpdate();
}

void FullScreenMessageActivity::onExit() {
#if defined(BOARD_ESP32_S3_EPAPER_397)
  alarmPlaybackStop();
  audioFilePlayer.stopAndWait();
#endif
  Activity::onExit();
}

bool FullScreenMessageActivity::preventAutoSleep() {
#if defined(BOARD_ESP32_S3_EPAPER_397)
  return alarmPlaybackIsActive();
#else
  return false;
#endif
}

void FullScreenMessageActivity::loop() {
#if defined(BOARD_ESP32_S3_EPAPER_397)
  if (consumeConfirmClick() || mappedInput.wasReleased(MappedInputManager::Button::Back)) {
#else
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm) ||
      mappedInput.wasReleased(MappedInputManager::Button::Back)) {
#endif
    finish();
  }
}

void FullScreenMessageActivity::render(RenderLock&&) {
  renderer.clearScreen();
  const auto screen = AppScreenLayout::listScreen(renderer);

  std::string combined;
  for (const std::string& paragraph : splitLines(text)) {
    if (!combined.empty()) {
      combined += '\n';
    }
    combined += paragraph;
  }
  AppScreenLayout::drawBodyText(renderer, screen.body, combined.c_str(), UI_10_FONT_ID, style);

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_OK_BUTTON), "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer(refreshMode);
}
