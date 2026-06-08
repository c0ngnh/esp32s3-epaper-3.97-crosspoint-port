#include "AlarmAlertActivity.h"

#if defined(BOARD_ESP32_S3_EPAPER_397)

#include <AlarmSound397.h>
#include <AudioFilePlayer.h>
#include <GfxRenderer.h>
#include <HalDisplay.h>
#include <I18n.h>

#include <algorithm>

#include <esp_random.h>

#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {

constexpr int kAlarmFontId = NOTOSANS_18_FONT_ID;
constexpr EpdFontFamily::Style kAlarmStyle = EpdFontFamily::BOLD;

int randomRange(const int lo, const int hi) {
  if (hi <= lo) {
    return lo;
  }
  return lo + static_cast<int>(esp_random() % static_cast<uint32_t>(hi - lo + 1));
}

}  // namespace

void AlarmAlertActivity::onEnter() {
  Activity::onEnter();
  showPhase_ = true;
  lastFrameMs_ = millis();
  requestUpdate();
}

void AlarmAlertActivity::onExit() {
  alarmPlaybackStop();
  audioFilePlayer.stopAndWait();
  Activity::onExit();
}

bool AlarmAlertActivity::preventAutoSleep() { return alarmPlaybackIsActive(); }

void AlarmAlertActivity::loop() {
  if (consumeConfirmClick() || mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    finish();
    return;
  }

  const uint32_t now = millis();
  if (now - lastFrameMs_ >= kFrameIntervalMs) {
    lastFrameMs_ = now;
    showPhase_ = !showPhase_;
    requestUpdate();
  }
}

void AlarmAlertActivity::render(RenderLock&&) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageW = renderer.getScreenWidth();
  const int pageH = renderer.getScreenHeight();
  const char* word = alertWord_ != nullptr ? alertWord_ : "ALARM";
  const int textW = renderer.getTextWidth(kAlarmFontId, word, kAlarmStyle);
  const int textH = renderer.getLineHeight(kAlarmFontId);
  const int pad = metrics.contentSidePadding;
  const int maxX = std::max(0, pageW - textW - pad);
  const int maxY = std::max(0, pageH - metrics.buttonHintsHeight - textH - pad);

  renderer.clearScreen();

  if (showPhase_) {
    const int count = randomRange(12, 28);
    for (int i = 0; i < count; ++i) {
      const int x = pad + randomRange(0, maxX);
      const int y = pad + randomRange(0, maxY);
      renderer.drawText(kAlarmFontId, x, y, word, true, kAlarmStyle);
    }
  }
  // hide phase_: cleared screen only — synced 2 Hz disappear before next pop-in

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_OK_BUTTON), "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
}

#endif
