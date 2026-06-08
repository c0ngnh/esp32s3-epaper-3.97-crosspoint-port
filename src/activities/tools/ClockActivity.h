#pragma once

#include "activities/Activity.h"
#include "components/themes/BaseTheme.h"
#include "util/ButtonNavigator.h"

#include <cstdint>
#include <string>
#include <vector>

class ClockActivity final : public Activity {
  enum class Mode : uint8_t { Hub, Alarms, AlarmEdit, AlarmSoundPick, AlarmWeekdaysPick, Stopwatch, Countdown };

  static constexpr int kAlarmFieldCount = 7;

  struct HoldAdjustState {
    uint32_t lastRepeatMs = 0;
    uint16_t repeatCount = 0;
    bool didRepeat = false;
  };

  ButtonNavigator buttonNavigator;
  Mode mode = Mode::Hub;
  int selectorIndex = 0;
  int alarmIndex = 0;
  bool alarmEditing = false;
  bool dirty = false;
  uint8_t lastRtcSecond = 255;
  unsigned long lastElapsedTickMs = 0;
  HoldAdjustState upHold_;
  HoldAdjustState downHold_;
  std::vector<std::string> musicTracks_;

  unsigned long stopwatchStartMs = 0;
  bool stopwatchRunning = false;
  uint32_t lastTimerDisplaySec = UINT32_MAX;
  bool tileFullRenderNeeded = true;

  unsigned long countdownEndMs = 0;
  int countdownSeconds = 30;
  bool countdownRunning = false;

  static constexpr int kCountdownMinSeconds = 1;
  static constexpr int kCountdownMaxSeconds = 3 * 24 * 60 * 60;

  static constexpr unsigned long HOLD_START_MS = 400;
  static constexpr unsigned long HOLD_INTERVAL_MIN_MS = 45;
  static constexpr unsigned long HOLD_INTERVAL_MAX_MS = 180;

  void saveAlarmsIfNeeded();
  void loadMusicTracks();
  void adjustAlarmField(int delta, int step);
  void adjustCountdownSeconds(int direction, int step);
  int stepForRepeat(int repeatCount) const;
  int countdownStepForRepeat(int repeatCount) const;
  unsigned long intervalForRepeat(int repeatCount) const;
  void pollHoldAdjust(MappedInputManager::Button button, int direction, HoldAdjustState& state);
  void pollCountdownHoldAdjust(MappedInputManager::Button button, int direction, HoldAdjustState& state);
  void loopAlarmListBrowse();
  void loopAlarmFieldBrowse();
  void loopAlarmFieldEdit();
  void loopAlarmSoundPick();
  void loopAlarmWeekdaysPick();
  void toggleWeekdayAt(int dayIndex);
  std::string weekdayValueForIndex(int dayIndex) const;
  void showAlarmWeekdaysPick();
  int soundPickerIndexForCurrent() const;
  void applySoundPickerSelection(int pickerIndex);
  std::string alarmValueForIndex(int index) const;
  void pollSecondRefresh();
  void showHub();
  void showAlarms();
  void showAlarmEdit();
  void showAlarmSoundPick();
  void showStopwatch();
  void showCountdown();
  void markTileFullRender();
  void patchTimerTile(const Rect& tile, unsigned long elapsedMs);
  bool pollCountdownExpired();
  void triggerCountdownFinished();

 public:
  explicit ClockActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("Clock", renderer, mappedInput) {}
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool preventAutoSleep() override {
    return (mode == Mode::Stopwatch && stopwatchRunning) || (mode == Mode::Countdown && countdownRunning);
  }
};
