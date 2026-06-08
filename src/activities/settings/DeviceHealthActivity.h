#pragma once

#if defined(BOARD_ESP32_S3_EPAPER_397)

#include "activities/Activity.h"

class DeviceHealthActivity final : public Activity {
  uint8_t lastRtcSecond = 255;

 public:
  explicit DeviceHealthActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("DeviceHealth", renderer, mappedInput) {}
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
};

#endif
