#pragma once

#if defined(BOARD_ESP32_S3_EPAPER_397)

#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

class ClockSettingsActivity final : public Activity {
 public:
  ClockSettingsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  struct HoldAdjustState {
    uint32_t lastRepeatMs = 0;
    uint16_t repeatCount = 0;
    bool didRepeat = false;
  };

  static constexpr int MENU_ITEMS = 7;
  static constexpr unsigned long HOLD_START_MS = 400;
  static constexpr unsigned long HOLD_INTERVAL_MIN_MS = 45;
  static constexpr unsigned long HOLD_INTERVAL_MAX_MS = 180;

  ButtonNavigator buttonNavigator;
  int selectedIndex = 0;
  bool editing = false;

  uint16_t year = 2026;
  uint8_t month = 1;
  uint8_t day = 1;
  uint8_t hour = 0;
  uint8_t minute = 0;
  int16_t timezoneOffsetMinutes = 0;
  uint8_t dstEnabled = 0;

  bool dirty = false;
  uint8_t lastRtcSecond = 255;
  HoldAdjustState upHold_;
  HoldAdjustState downHold_;

  void pollSecondRefresh();
  void loadFromRtc();
  void saveToRtc();
  void adjustField(int delta, int step);
  int stepForRepeat(int repeatCount) const;
  unsigned long intervalForRepeat(int repeatCount) const;
  void pollHoldAdjust(MappedInputManager::Button button, int direction, HoldAdjustState& state);
  void loopBrowse();
  void loopEdit();
  std::string formatTimezone() const;
  std::string valueForIndex(int index) const;
  std::string listRowValue(int index) const;
};

#endif
