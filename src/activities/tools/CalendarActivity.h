#pragma once

#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

class CalendarActivity final : public Activity {
  enum class Mode : uint8_t { Month, Convert };

  struct HoldAdjustState {
    uint32_t lastRepeatMs = 0;
    uint16_t repeatCount = 0;
    bool didRepeat = false;
  };

  static constexpr int kConvertMenuItems = 4;
  static constexpr unsigned long HOLD_START_MS = 400;
  static constexpr unsigned long HOLD_INTERVAL_MIN_MS = 45;
  static constexpr unsigned long HOLD_INTERVAL_MAX_MS = 180;

  ButtonNavigator buttonNavigator;
  Mode mode = Mode::Month;
  int viewYear = 2026;
  int viewMonth = 1;
  int selectedDay = 0;
  bool convertSourceGregorian = true;
  bool convertEditing = false;
  int convertField = 0;
  int gregYear = 2026;
  int gregMonth = 1;
  int gregDay = 1;
  int lunarYear = 2026;
  int lunarMonth = 1;
  int lunarDay = 1;
  bool lunarLeap = false;
  HoldAdjustState upHold_;
  HoldAdjustState downHold_;

  void syncLunarFromGregorian();
  void syncGregorianFromLunar();
  void clampSelectedDayInViewMonth();
  void bumpViewMonth(int delta);
  void moveSelectedDay(int delta);
  void moveSelectedDayByWeeks(int weeks);
  void pollMonthTiltNav();
  void adjustSourceField(int delta, int step);
  int stepForRepeat(int repeatCount) const;
  unsigned long intervalForRepeat(int repeatCount) const;
  void pollHoldAdjust(MappedInputManager::Button button, int direction, HoldAdjustState& state);
  void loopConvertBrowse();
  void loopConvertEdit();
  void loopConvert();
  bool isViewingToday() const;
  void goToToday();
  void loopMonthView();
  void showMonth();
  void showConvert();

 public:
  explicit CalendarActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("Calendar", renderer, mappedInput) {}
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
};
