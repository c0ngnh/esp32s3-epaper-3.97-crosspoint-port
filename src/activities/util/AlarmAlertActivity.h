#pragma once

#include <HalDisplay.h>

#include "activities/Activity.h"

#if defined(BOARD_ESP32_S3_EPAPER_397)

class AlarmAlertActivity final : public Activity {
  const char* alertWord_ = "ALARM";
  bool showPhase_ = true;
  uint32_t lastFrameMs_ = 0;

  static constexpr uint32_t kFrameIntervalMs = 500;  // 2 Hz

 public:
  explicit AlarmAlertActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                            const char* alertWord = "ALARM")
      : Activity("AlarmAlert", renderer, mappedInput), alertWord_(alertWord) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool preventAutoSleep() override;
  bool skipLoopDelay() override { return true; }
};

#endif
