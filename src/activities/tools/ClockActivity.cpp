#include "ClockActivity.h"

#if defined(BOARD_ESP32_S3_EPAPER_397)

#include <AlarmSound397.h>
#include <AudioFilePlayer.h>
#include <HalBoard397.h>
#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "ClockStore.h"
#include "MappedInputManager.h"
#include "activities/reader/ReaderUtils.h"
#include "components/UITheme.h"
#include "components/themes/BaseTheme.h"
#include "fontIds.h"
#include "util/MusicTrackList.h"
#include "util/UnifiedAppLayout.h"

namespace {

constexpr StrId kAlarmFieldNames[] = {StrId::STR_CLOCK_ENABLED,
                                      StrId::STR_CLOCK_HOUR,
                                      StrId::STR_CLOCK_MINUTE,
                                      StrId::STR_CLOCK_ALARM_REPEAT,
                                      StrId::STR_CLOCK_ALARM_WEEKDAYS,
                                      StrId::STR_CLOCK_ALARM_SOUND,
                                      StrId::STR_CLOCK_ALARM_SOUND_DURATION};

const char* repeatLabel(const AlarmRepeat repeat) {
  switch (repeat) {
    case AlarmRepeat::Once:
      return tr(STR_CLOCK_ALARM_REPEAT_ONCE);
    case AlarmRepeat::Daily:
      return tr(STR_CLOCK_ALARM_REPEAT_DAILY);
    case AlarmRepeat::Weekly:
      return tr(STR_CLOCK_ALARM_REPEAT_WEEKLY);
  }
  return tr(STR_CLOCK_ALARM_REPEAT_DAILY);
}

constexpr StrId kWeekdayPickerNames[] = {StrId::STR_WEEKDAY_SUN, StrId::STR_WEEKDAY_MON, StrId::STR_WEEKDAY_TUE,
                                         StrId::STR_WEEKDAY_WED, StrId::STR_WEEKDAY_THU, StrId::STR_WEEKDAY_FRI,
                                         StrId::STR_WEEKDAY_SAT};

constexpr int kWeekdayPickerCount = 7;

std::string formatWeekdays(const uint8_t mask) {
  static const char* kShort[] = {"Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"};
  static constexpr uint8_t kBitOrder[7] = {1, 2, 3, 4, 5, 6, 0};  // display Mon..Sun, bit0=Sun
  std::string out;
  for (int di = 0; di < 7; ++di) {
    const int bit = kBitOrder[di];
    if ((mask & static_cast<uint8_t>(1u << bit)) != 0) {
      if (!out.empty()) {
        out += ", ";
      }
      out += kShort[di];
    }
  }
  if (out.empty()) {
    out = tr(STR_CLOCK_ALARM_WEEKDAYS_NONE);
  }
  return out;
}
constexpr int kSoundPickerCount(const size_t trackCount) {
  return static_cast<int>(1 + trackCount);
}

std::string formatAlarmMenuRow(const ClockAlarm& alarm) {
  char buf[64];
  snprintf(buf, sizeof(buf), "%s  %02u:%02u  %s", alarm.enabled ? tr(STR_STATE_ON) : tr(STR_STATE_OFF), alarm.hour,
           alarm.minute, repeatLabel(alarm.repeat));
  std::string row(buf);
  if (alarm.repeat == AlarmRepeat::Weekly) {
    row += "  ";
    row += formatWeekdays(alarm.weekdays);
  }
  return row;
}

UIIcon hubMenuIcon(const int index) {
  switch (index) {
    case 0:
      return UIIcon::Alarm;
    case 1:
      return UIIcon::Stopwatch;
    case 2:
      return UIIcon::Countdown;
    default:
      return UIIcon::Clock;
  }
}

void formatCountdownDuration(const int totalSec, char* buf, const size_t bufLen) {
  if (totalSec < 60) {
    snprintf(buf, bufLen, "%d s", totalSec);
  } else if (totalSec < 3600) {
    const int m = totalSec / 60;
    const int s = totalSec % 60;
    if (s == 0) {
      snprintf(buf, bufLen, "%d m", m);
    } else {
      snprintf(buf, bufLen, "%d m %d s", m, s);
    }
  } else if (totalSec < 86400) {
    const int h = totalSec / 3600;
    const int m = (totalSec % 3600) / 60;
    if (m == 0) {
      snprintf(buf, bufLen, "%d h", h);
    } else {
      snprintf(buf, bufLen, "%d h %d m", h, m);
    }
  } else {
    const int d = totalSec / 86400;
    const int h = (totalSec % 86400) / 3600;
    if (h == 0) {
      snprintf(buf, bufLen, "%d d", d);
    } else {
      snprintf(buf, bufLen, "%d d %d h", d, h);
    }
  }
}

void drawCountdownProgressBar(const GfxRenderer& renderer, const Rect& menu, const unsigned long leftMs,
                              const int totalSeconds) {
  if (totalSeconds <= 0 || menu.height < 24) {
    return;
  }
  const auto& m = UITheme::getInstance().getMetrics();
  const int barH = m.progressBarHeight;
  const int barW = std::max(40, menu.width - (m.contentSidePadding + 12) * 2);
  const int barX = menu.x + (menu.width - barW) / 2;
  const int barY = menu.y + menu.height - barH - 10;
  renderer.drawRect(barX, barY, barW, barH, true);
  const size_t totalMs = static_cast<size_t>(totalSeconds) * 1000UL;
  const size_t elapsedMs = (leftMs >= totalMs) ? 0 : (totalMs - leftMs);
  const int fillW = (barW - 4) * static_cast<int>(elapsedMs) / static_cast<int>(totalMs);
  if (fillW > 0) {
    renderer.fillRect(barX + 2, barY + 2, fillW, barH - 4, true);
  }
}
}  // namespace

void ClockActivity::markTileFullRender() {
  tileFullRenderNeeded = true;
  lastTimerDisplaySec = UINT32_MAX;
}

void ClockActivity::patchTimerTile(const Rect& tile, const unsigned long elapsedMs) {
  UnifiedAppLayout::patchBigElapsedMsTile(renderer, tile, elapsedMs);
}

void ClockActivity::loadMusicTracks() { listMusicTracks(musicTracks_); }

void ClockActivity::onEnter() {
  Activity::onEnter();
  CLOCK_STORE.loadFromFile();
  loadMusicTracks();
  mode = Mode::Hub;
  selectorIndex = 0;
  alarmEditing = false;
  lastRtcSecond = 255;
  lastElapsedTickMs = millis();
  markTileFullRender();
  requestUpdate();
}

void ClockActivity::onExit() {
  CLOCK_STORE.setAlarmCheckSuspended(false);
  saveAlarmsIfNeeded();
  Activity::onExit();
}

void ClockActivity::saveAlarmsIfNeeded() {
  if (dirty) {
    CLOCK_STORE.saveToFile();
    dirty = false;
  }
}

void ClockActivity::triggerCountdownFinished() {
  if (!countdownRunning) {
    return;
  }
  countdownRunning = false;
  countdownEndMs = 0;
  audioFilePlayer.stopAndWait();
  alarmPlaybackStart(nullptr, 60);
  activityManager.goToAlarmAlert("COUNTDOWN");
}

bool ClockActivity::pollCountdownExpired() {
  if (mode == Mode::Countdown && countdownRunning && millis() >= countdownEndMs) {
    triggerCountdownFinished();
    return true;
  }
  return false;
}

void ClockActivity::pollSecondRefresh() {
  if (mode == Mode::Hub || mode == Mode::Alarms) {
    if (UnifiedAppLayout::pollRtcSecondTick(lastRtcSecond)) {
      requestUpdate();
    }
    return;
  }
  if (mode == Mode::Stopwatch || mode == Mode::Countdown) {
    if (stopwatchRunning || countdownRunning) {
      if (UnifiedAppLayout::pollElapsedSecondTick(lastElapsedTickMs)) {
        requestUpdate();
      }
    }
  }
}

void ClockActivity::adjustAlarmField(const int delta, const int step) {
  if (alarmIndex < 0 || alarmIndex >= static_cast<int>(CLOCK_STORE.alarms().size()) || step <= 0) {
    return;
  }
  ClockAlarm& a = CLOCK_STORE.alarms()[alarmIndex];
  dirty = true;
  const int change = delta * step;
  switch (selectorIndex) {
    case 0:
      if (change > 0) {
        if (a.repeat == AlarmRepeat::Weekly && a.weekdays == 0) {
          a.weekdays = 0x7F;
        }
        a.enabled = true;
        if (a.repeat == AlarmRepeat::Once) {
          a.onceFired = false;
        }
      } else if (change < 0) {
        a.enabled = false;
      }
      break;
    case 1:
      a.hour = static_cast<uint8_t>((static_cast<int>(a.hour) + change % 24 + 24) % 24);
      if (a.repeat == AlarmRepeat::Once && a.enabled) {
        a.onceFired = false;
      }
      break;
    case 2:
      a.minute = static_cast<uint8_t>((static_cast<int>(a.minute) + change % 60 + 60) % 60);
      if (a.repeat == AlarmRepeat::Once && a.enabled) {
        a.onceFired = false;
      }
      break;
    case 3: {
      int next = static_cast<int>(a.repeat) + change;
      next = (next % 3 + 3) % 3;
      a.repeat = static_cast<AlarmRepeat>(next);
      if (a.repeat != AlarmRepeat::Once) {
        a.onceFired = false;
      }
      if (a.repeat == AlarmRepeat::Daily) {
        a.weekdays = 0x7F;
      } else if (a.repeat == AlarmRepeat::Weekly && a.weekdays == 0) {
        a.weekdays = 0x7F;
      }
      break;
    }
    case 4:
      break;
    case 5: {
      if (musicTracks_.empty()) {
        loadMusicTracks();
      }
      const int count = kSoundPickerCount(musicTracks_.size());
      int idx = soundPickerIndexForCurrent();
      idx = (idx + change % count + count) % count;
      applySoundPickerSelection(idx);
      break;
    }
    case 6: {
      int next = static_cast<int>(a.soundDurationSec) + change * step;
      next = std::clamp(next, 5, 600);
      a.soundDurationSec = static_cast<uint16_t>(next);
      break;
    }
    default:
      break;
  }
}

int ClockActivity::stepForRepeat(const int repeatCount) const {
  if (selectorIndex == 0 || selectorIndex == 3) {
    return 1;
  }
  if (selectorIndex == 6) {
    int step = 5 + (repeatCount / 3) * 5;
    return std::min(step, 60);
  }

  int step = 5 + (repeatCount / 3) * 5;
  const int maxStep = (selectorIndex == 1) ? 12 : 30;
  return std::min(step, maxStep);
}

void ClockActivity::adjustCountdownSeconds(const int direction, const int step) {
  if (direction == 0 || step <= 0) {
    return;
  }
  const long next = static_cast<long>(countdownSeconds) + static_cast<long>(direction) * step;
  countdownSeconds =
      static_cast<int>(std::clamp(next, static_cast<long>(kCountdownMinSeconds), static_cast<long>(kCountdownMaxSeconds)));
}

int ClockActivity::countdownStepForRepeat(const int repeatCount) const {
  if (repeatCount < 3) {
    return 1;
  }
  int step = 5 + ((repeatCount - 3) / 3) * 5;
  if (repeatCount >= 9) {
    step = 30 + ((repeatCount - 9) / 3) * 30;
  }
  if (repeatCount >= 15) {
    step = 300 + ((repeatCount - 15) / 3) * 300;
  }
  if (repeatCount >= 21) {
    step = 3600 + ((repeatCount - 21) / 3) * 3600;
  }
  return std::min(step, 3600);
}

unsigned long ClockActivity::intervalForRepeat(const int repeatCount) const {
  unsigned long interval = HOLD_INTERVAL_MAX_MS;
  if (repeatCount > 6) {
    interval = 120;
  }
  if (repeatCount > 12) {
    interval = 90;
  }
  if (repeatCount > 20) {
    interval = 65;
  }
  if (repeatCount > 28) {
    interval = HOLD_INTERVAL_MIN_MS;
  }
  return interval;
}

void ClockActivity::pollHoldAdjust(const MappedInputManager::Button button, const int direction,
                                     HoldAdjustState& state) {
  const bool isUp = button == MappedInputManager::Button::Up;
  const bool isDown = button == MappedInputManager::Button::Down;
  if (!isUp && !isDown) {
    return;
  }

  if (mappedInput.wasPressed(button)) {
    state.lastRepeatMs = 0;
    state.repeatCount = 0;
    state.didRepeat = false;
  }

  if (mappedInput.wasReleased(button)) {
    if (!state.didRepeat) {
      adjustAlarmField(direction, 1);
      requestUpdate();
    }
    state.lastRepeatMs = 0;
    state.repeatCount = 0;
    state.didRepeat = false;
    return;
  }

  if (!mappedInput.isPressed(button)) {
    return;
  }

  if (mappedInput.getHeldTime() < HOLD_START_MS) {
    return;
  }

  const uint32_t now = millis();
  if (state.lastRepeatMs == 0) {
    state.lastRepeatMs = now;
    state.repeatCount = 0;
  }

  if (now - state.lastRepeatMs < intervalForRepeat(static_cast<int>(state.repeatCount))) {
    return;
  }

  const int step = stepForRepeat(static_cast<int>(state.repeatCount));
  adjustAlarmField(direction, step);
  state.didRepeat = true;
  state.repeatCount++;
  state.lastRepeatMs = now;
  requestUpdate();
}

void ClockActivity::pollCountdownHoldAdjust(const MappedInputManager::Button button, const int direction,
                                            HoldAdjustState& state) {
  const bool isUp = button == MappedInputManager::Button::Up;
  const bool isDown = button == MappedInputManager::Button::Down;
  if (!isUp && !isDown) {
    return;
  }

  if (mappedInput.wasPressed(button)) {
    state.lastRepeatMs = 0;
    state.repeatCount = 0;
    state.didRepeat = false;
  }

  if (mappedInput.wasReleased(button)) {
    if (!state.didRepeat) {
      adjustCountdownSeconds(direction, 1);
      markTileFullRender();
      requestUpdate();
    }
    state.lastRepeatMs = 0;
    state.repeatCount = 0;
    state.didRepeat = false;
    return;
  }

  if (!mappedInput.isPressed(button)) {
    return;
  }

  if (mappedInput.getHeldTime() < HOLD_START_MS) {
    return;
  }

  const uint32_t now = millis();
  if (state.lastRepeatMs == 0) {
    state.lastRepeatMs = now;
    state.repeatCount = 0;
  }

  if (now - state.lastRepeatMs < intervalForRepeat(static_cast<int>(state.repeatCount))) {
    return;
  }

  const int step = countdownStepForRepeat(static_cast<int>(state.repeatCount));
  adjustCountdownSeconds(direction, step);
  state.didRepeat = true;
  state.repeatCount++;
  state.lastRepeatMs = now;
  markTileFullRender();
  requestUpdate();
}

void ClockActivity::loopAlarmListBrowse() {
  const int count = static_cast<int>(CLOCK_STORE.alarms().size());
  buttonNavigator.onNextRelease([this, count] {
    selectorIndex = ButtonNavigator::nextIndex(selectorIndex, count);
    requestUpdate();
  });
  buttonNavigator.onPreviousRelease([this, count] {
    selectorIndex = ButtonNavigator::previousIndex(selectorIndex, count);
    requestUpdate();
  });
}

void ClockActivity::loopAlarmFieldBrowse() {
  buttonNavigator.onNextRelease([this] {
    selectorIndex = ButtonNavigator::nextIndex(selectorIndex, kAlarmFieldCount);
    requestUpdate();
  });
  buttonNavigator.onPreviousRelease([this] {
    selectorIndex = ButtonNavigator::previousIndex(selectorIndex, kAlarmFieldCount);
    requestUpdate();
  });
}

void ClockActivity::loopAlarmSoundPick() {
  const int count = kSoundPickerCount(musicTracks_.size());
  buttonNavigator.onNextRelease([this, count] {
    selectorIndex = ButtonNavigator::nextIndex(selectorIndex, count);
    requestUpdate();
  });
  buttonNavigator.onPreviousRelease([this, count] {
    selectorIndex = ButtonNavigator::previousIndex(selectorIndex, count);
    requestUpdate();
  });
}

void ClockActivity::toggleWeekdayAt(const int dayIndex) {
  if (alarmIndex < 0 || alarmIndex >= static_cast<int>(CLOCK_STORE.alarms().size()) || dayIndex < 0 ||
      dayIndex >= kWeekdayPickerCount) {
    return;
  }
  ClockAlarm& a = CLOCK_STORE.alarms()[alarmIndex];
  a.weekdays ^= static_cast<uint8_t>(1u << dayIndex);
  if (a.repeat == AlarmRepeat::Weekly && a.weekdays == 0) {
    a.enabled = false;
  }
  dirty = true;
}

std::string ClockActivity::weekdayValueForIndex(const int dayIndex) const {
  if (alarmIndex < 0 || alarmIndex >= static_cast<int>(CLOCK_STORE.alarms().size()) || dayIndex < 0 ||
      dayIndex >= kWeekdayPickerCount) {
    return {};
  }
  const auto& a = CLOCK_STORE.alarms()[alarmIndex];
  const bool on = (a.weekdays & static_cast<uint8_t>(1u << dayIndex)) != 0;
  return std::string(on ? tr(STR_STATE_ON) : tr(STR_STATE_OFF));
}

void ClockActivity::loopAlarmWeekdaysPick() {
  buttonNavigator.onNextRelease([this] {
    selectorIndex = ButtonNavigator::nextIndex(selectorIndex, kWeekdayPickerCount);
    requestUpdate();
  });
  buttonNavigator.onPreviousRelease([this] {
    selectorIndex = ButtonNavigator::previousIndex(selectorIndex, kWeekdayPickerCount);
    requestUpdate();
  });
}

int ClockActivity::soundPickerIndexForCurrent() const {
  if (alarmIndex < 0 || alarmIndex >= static_cast<int>(CLOCK_STORE.alarms().size())) {
    return 0;
  }
  const auto& a = CLOCK_STORE.alarms()[alarmIndex];
  if (a.soundFile[0] == '\0') {
    return 0;
  }
  for (size_t i = 0; i < musicTracks_.size(); ++i) {
    if (musicTracks_[i] == a.soundFile) {
      return static_cast<int>(i + 1);
    }
  }
  return 0;
}

void ClockActivity::applySoundPickerSelection(const int pickerIndex) {
  if (alarmIndex < 0 || alarmIndex >= static_cast<int>(CLOCK_STORE.alarms().size())) {
    return;
  }
  ClockAlarm& a = CLOCK_STORE.alarms()[alarmIndex];
  dirty = true;
  if (pickerIndex <= 0) {
    a.soundFile[0] = '\0';
    return;
  }
  const size_t trackIdx = static_cast<size_t>(pickerIndex - 1);
  if (trackIdx >= musicTracks_.size()) {
    return;
  }
  strncpy(a.soundFile, musicTracks_[trackIdx].c_str(), sizeof(a.soundFile) - 1);
  a.soundFile[sizeof(a.soundFile) - 1] = '\0';
}

void ClockActivity::loopAlarmFieldEdit() {
  pollHoldAdjust(MappedInputManager::Button::Up, +1, upHold_);
  pollHoldAdjust(MappedInputManager::Button::Down, -1, downHold_);
}

std::string ClockActivity::alarmValueForIndex(const int index) const {
  if (alarmIndex < 0 || alarmIndex >= static_cast<int>(CLOCK_STORE.alarms().size())) {
    return {};
  }
  const auto& a = CLOCK_STORE.alarms()[alarmIndex];
  switch (index) {
    case 0:
      return std::string(a.enabled ? tr(STR_STATE_ON) : tr(STR_STATE_OFF));
    case 1: {
      char buf[8];
      snprintf(buf, sizeof(buf), "%02u", a.hour);
      return buf;
    }
    case 2: {
      char buf[8];
      snprintf(buf, sizeof(buf), "%02u", a.minute);
      return buf;
    }
    case 3:
      return repeatLabel(a.repeat);
    case 4:
      if (a.repeat != AlarmRepeat::Weekly) {
        return std::string("-");
      }
      return formatWeekdays(a.weekdays);
    case 5:
      if (a.soundFile[0] == '\0') {
        return tr(STR_CLOCK_ALARM_SOUND_DEFAULT);
      }
      return renderer.truncatedText(UI_10_FONT_ID, a.soundFile, 200, EpdFontFamily::REGULAR);
    case 6: {
      char buf[16];
      snprintf(buf, sizeof(buf), "%u s", static_cast<unsigned>(a.soundDurationSec));
      return buf;
    }
    default:
      return {};
  }
}

void ClockActivity::loop() {
  CLOCK_STORE.setAlarmCheckSuspended(mode == Mode::AlarmEdit || mode == Mode::AlarmSoundPick ||
                                     mode == Mode::AlarmWeekdaysPick);
  pollSecondRefresh();
  if (pollCountdownExpired()) {
    return;
  }

  if (mode == Mode::Countdown && !countdownRunning) {
    pollCountdownHoldAdjust(MappedInputManager::Button::Up, 1, upHold_);
    pollCountdownHoldAdjust(MappedInputManager::Button::Down, -1, downHold_);
  }

  if (mode == Mode::AlarmSoundPick) {
    loopAlarmSoundPick();
    if (mappedInput.wasBackClicked()) {
      selectorIndex = 5;
      mode = Mode::AlarmEdit;
      requestUpdate();
      return;
    }
    if (consumeConfirmClick()) {
      applySoundPickerSelection(selectorIndex);
      saveAlarmsIfNeeded();
      selectorIndex = 5;
      mode = Mode::AlarmEdit;
      requestUpdate();
    }
    return;
  }

  if (mode == Mode::AlarmWeekdaysPick) {
    loopAlarmWeekdaysPick();
    if (mappedInput.wasBackClicked()) {
      saveAlarmsIfNeeded();
      selectorIndex = 4;
      mode = Mode::AlarmEdit;
      requestUpdate();
      return;
    }
    if (consumeConfirmClick()) {
      toggleWeekdayAt(selectorIndex);
      requestUpdate();
    }
    return;
  }

  if (mode == Mode::AlarmEdit) {
    if (mappedInput.wasBackClicked()) {
      if (alarmEditing) {
        alarmEditing = false;
        upHold_ = {};
        downHold_ = {};
        requestUpdate();
        return;
      }
      saveAlarmsIfNeeded();
      CLOCK_STORE.resetAlarmMinuteLatch();
      mode = Mode::Alarms;
      requestUpdate();
      return;
    }

    if (consumeConfirmClick()) {
      if (selectorIndex == 4 && alarmIndex >= 0 && alarmIndex < static_cast<int>(CLOCK_STORE.alarms().size()) &&
          CLOCK_STORE.alarms()[alarmIndex].repeat == AlarmRepeat::Weekly) {
        alarmEditing = false;
        upHold_ = {};
        downHold_ = {};
        selectorIndex = 0;
        mode = Mode::AlarmWeekdaysPick;
        requestUpdate();
        return;
      }
      if (!alarmEditing && selectorIndex == 5) {
        loadMusicTracks();
        selectorIndex = soundPickerIndexForCurrent();
        mode = Mode::AlarmSoundPick;
        requestUpdate();
        return;
      }
      alarmEditing = !alarmEditing;
      upHold_ = {};
      downHold_ = {};
      if (!alarmEditing) {
        saveAlarmsIfNeeded();
        CLOCK_STORE.resetAlarmMinuteLatch();
      }
      requestUpdate();
      return;
    }

    if (alarmEditing) {
      loopAlarmFieldEdit();
    } else {
      loopAlarmFieldBrowse();
    }
    return;
  }

  if (mode == Mode::Alarms) {
    loopAlarmListBrowse();
  } else if (mode == Mode::Hub) {
    buttonNavigator.onNext([this] {
      selectorIndex = ButtonNavigator::nextIndex(selectorIndex, 3);
      requestUpdate();
    });
    buttonNavigator.onPrevious([this] {
      selectorIndex = ButtonNavigator::previousIndex(selectorIndex, 3);
      requestUpdate();
    });
  }

  if (ReaderUtils::wasShortBackClicked(mappedInput)) {
    if (mode == Mode::Stopwatch) {
      const unsigned long elapsed = stopwatchRunning ? (millis() - stopwatchStartMs) : 0;
      if (elapsed > 0 || stopwatchRunning) {
        stopwatchRunning = false;
        stopwatchStartMs = 0;
        lastElapsedTickMs = millis();
        markTileFullRender();
        requestUpdate();
        return;
      }
      mode = Mode::Hub;
      selectorIndex = 0;
      markTileFullRender();
      requestUpdate();
      return;
    }
    if (mode == Mode::Countdown) {
      unsigned long leftMs = 0;
      if (countdownRunning && millis() < countdownEndMs) {
        leftMs = countdownEndMs - millis();
      }
      if (leftMs > 0 || countdownRunning) {
        countdownRunning = false;
        countdownEndMs = 0;
        lastElapsedTickMs = millis();
        markTileFullRender();
        requestUpdate();
        return;
      }
      mode = Mode::Hub;
      selectorIndex = 0;
      markTileFullRender();
      requestUpdate();
      return;
    }
    if (mode == Mode::Hub) {
      finish();
      return;
    }
    mode = Mode::Hub;
    selectorIndex = 0;
    markTileFullRender();
    requestUpdate();
    return;
  }

  if (!consumeConfirmClick()) {
    return;
  }

  switch (mode) {
    case Mode::Hub:
      if (selectorIndex == 0) {
        mode = Mode::Alarms;
        selectorIndex = 0;
        lastRtcSecond = 255;
        markTileFullRender();
      } else if (selectorIndex == 1) {
        mode = Mode::Stopwatch;
        stopwatchRunning = false;
        stopwatchStartMs = 0;
        lastElapsedTickMs = millis();
        markTileFullRender();
      } else if (selectorIndex == 2) {
        mode = Mode::Countdown;
        countdownSeconds = 30;
        countdownRunning = false;
        countdownEndMs = 0;
        upHold_ = {};
        downHold_ = {};
        lastElapsedTickMs = millis();
        markTileFullRender();
      }
      requestUpdate();
      break;
    case Mode::Alarms:
      alarmIndex = selectorIndex;
      mode = Mode::AlarmEdit;
      selectorIndex = 0;
      alarmEditing = false;
      upHold_ = {};
      downHold_ = {};
      requestUpdate();
      break;
    case Mode::Stopwatch:
      if (!stopwatchRunning) {
        stopwatchStartMs = millis();
        stopwatchRunning = true;
        lastElapsedTickMs = millis();
      } else {
        stopwatchRunning = false;
      }
      markTileFullRender();
      requestUpdate();
      break;
    case Mode::Countdown:
      if (!countdownRunning) {
        countdownEndMs = millis() + static_cast<unsigned long>(countdownSeconds) * 1000UL;
        countdownRunning = true;
        lastElapsedTickMs = millis();
      } else {
        countdownRunning = false;
      }
      markTileFullRender();
      requestUpdate();
      break;
  }
}

void ClockActivity::render(RenderLock&&) {
  const bool timerMode = mode == Mode::Stopwatch || mode == Mode::Countdown;
  if (!timerMode || tileFullRenderNeeded) {
    renderer.clearScreen();
    switch (mode) {
      case Mode::Hub:
        showHub();
        break;
      case Mode::Alarms:
        showAlarms();
        break;
      case Mode::AlarmEdit:
        showAlarmEdit();
        break;
      case Mode::AlarmSoundPick:
        showAlarmSoundPick();
        break;
      case Mode::AlarmWeekdaysPick:
        showAlarmWeekdaysPick();
        break;
      case Mode::Stopwatch:
        showStopwatch();
        break;
      case Mode::Countdown:
        showCountdown();
        break;
    }
    tileFullRenderNeeded = false;
    if (timerMode) {
      unsigned long ms = 0;
      if (mode == Mode::Stopwatch && stopwatchRunning) {
        ms = millis() - stopwatchStartMs;
      } else if (mode == Mode::Countdown && countdownRunning && millis() < countdownEndMs) {
        ms = countdownEndMs - millis();
      }
      lastTimerDisplaySec = static_cast<uint32_t>(ms / 1000UL);
    }
  } else {
    const auto& m = UITheme::getInstance().getMetrics();
    const int headerBottom = m.topPadding + m.headerHeight;
    const auto layout = UnifiedAppLayout::splitBelowHeader(renderer, headerBottom);
    unsigned long displayMs = 0;
    bool fullMenuRefresh = false;
    if (mode == Mode::Stopwatch && stopwatchRunning) {
      displayMs = millis() - stopwatchStartMs;
    } else if (mode == Mode::Countdown) {
      if (countdownRunning && millis() < countdownEndMs) {
        displayMs = countdownEndMs - millis();
      } else if (countdownRunning) {
        pollCountdownExpired();
        return;
      }
    }
    const uint32_t sec = static_cast<uint32_t>(displayMs / 1000UL);
    if (!fullMenuRefresh && sec != lastTimerDisplaySec) {
      patchTimerTile(layout.bigTile, displayMs);
      renderer.fillRect(layout.menu.x, layout.menu.y, layout.menu.width, layout.menu.height, false);
      if (mode == Mode::Stopwatch) {
        const char* line1 =
            stopwatchRunning ? tr(STR_CLOCK_PAUSE) : tr(STR_CLOCK_START);
        char line2[48];
        snprintf(line2, sizeof(line2), "%s: %s", tr(STR_SELECT),
                 stopwatchRunning ? tr(STR_CLOCK_PAUSE) : tr(STR_CLOCK_START));
        const char* line3 =
            (displayMs > 0 || stopwatchRunning) ? tr(STR_CLOCK_RESET) : tr(STR_BACK);
        UnifiedAppLayout::drawMenuHintPanel(renderer, layout.menu, line1, line2, line3);
      } else {
        char durVal[32];
        formatCountdownDuration(countdownSeconds, durVal, sizeof(durVal));
        char durLine[48];
        snprintf(durLine, sizeof(durLine), "%s: %s", tr(STR_CLOCK_DURATION), durVal);
        char line2[48];
        snprintf(line2, sizeof(line2), "%s: %s", tr(STR_SELECT),
                 countdownRunning ? tr(STR_CLOCK_PAUSE) : tr(STR_CLOCK_START));
        const char* line3 =
            (displayMs > 0 || countdownRunning) ? tr(STR_CLOCK_RESET) : tr(STR_BACK);
        if (countdownRunning) {
          UnifiedAppLayout::drawMenuHintPanel(renderer, layout.menu, durLine, line2, line3);
          drawCountdownProgressBar(renderer, layout.menu, displayMs, countdownSeconds);
        } else {
          UnifiedAppLayout::drawMenuHintPanel(renderer, layout.menu, durLine, tr(STR_CLOCK_ADJUST_HINT), line2);
        }
      }
      lastTimerDisplaySec = sec;
    }
  }
  renderer.displayBuffer();
}

void ClockActivity::showHub() {
  const auto& m = UITheme::getInstance().getMetrics();
  const int w = renderer.getScreenWidth();
  const int headerBottom = m.topPadding + m.headerHeight;
  GUI.drawHeader(renderer, Rect{0, m.topPadding, w, m.headerHeight}, tr(STR_CLOCK_APP));
  const auto layout = UnifiedAppLayout::splitBelowHeader(renderer, headerBottom);
  UnifiedAppLayout::drawBigRtcClockTile(renderer, layout.bigTile);
  UnifiedAppLayout::drawRtcDateCaptionInTile(renderer, layout.bigTile);
  const char* rows[] = {tr(STR_CLOCK_ALARMS), tr(STR_CLOCK_STOPWATCH), tr(STR_CLOCK_COUNTDOWN)};
  GUI.drawButtonMenu(renderer, layout.menu, 3, selectorIndex, [&](int i) { return rows[i]; },
                     [](int i) { return hubMenuIcon(i); }, UnifiedAppLayout::kMenuVisibleRows);
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_OPEN), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}

void ClockActivity::showAlarms() {
  const auto& m = UITheme::getInstance().getMetrics();
  const int w = renderer.getScreenWidth();
  const int pageH = renderer.getScreenHeight();
  const int headerBottom = m.topPadding + m.headerHeight;
  GUI.drawHeader(renderer, Rect{0, m.topPadding, w, m.headerHeight}, tr(STR_CLOCK_ALARMS));
  const int contentTop = headerBottom + m.verticalSpacing;
  const int menuBottom = pageH - m.buttonHintsHeight - m.verticalSpacing;
  const Rect menu{0, contentTop, w, std::max(0, menuBottom - contentTop)};
  auto& alarms = CLOCK_STORE.alarms();
  const int count = static_cast<int>(alarms.size());
  GUI.drawButtonMenu(renderer, menu, count, selectorIndex,
                     [&](int i) { return formatAlarmMenuRow(alarms[i]); }, nullptr,
                     UnifiedAppLayout::kAlarmVisibleRows);
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_EDIT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}

void ClockActivity::showAlarmEdit() {
  const auto& m = UITheme::getInstance().getMetrics();
  const int w = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();
  GUI.drawHeader(renderer, Rect{0, m.topPadding, w, m.headerHeight}, tr(STR_CLOCK_ALARM_EDIT));
  if (alarmIndex < 0 || alarmIndex >= static_cast<int>(CLOCK_STORE.alarms().size())) {
    return;
  }

  const int contentTop = m.topPadding + m.headerHeight + m.verticalSpacing;
  const int contentHeight = pageHeight - contentTop - m.buttonHintsHeight - m.verticalSpacing * 2;

  GUI.drawList(renderer, Rect{0, contentTop, w, contentHeight}, kAlarmFieldCount, selectorIndex,
               [](int index) { return std::string(I18N.get(kAlarmFieldNames[index])); }, nullptr, nullptr,
               [this](int index) { return alarmValueForIndex(index); }, alarmEditing);

  MappedInputManager::Labels labels;
  if (alarmEditing) {
    labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_DONE), "+", "-");
  } else {
    labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_CLOCK_EDIT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  }
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}

void ClockActivity::showAlarmWeekdaysPick() {
  const auto& m = UITheme::getInstance().getMetrics();
  const int w = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();
  GUI.drawHeader(renderer, Rect{0, m.topPadding, w, m.headerHeight}, tr(STR_CLOCK_ALARM_WEEKDAYS));

  const int contentTop = m.topPadding + m.headerHeight + m.verticalSpacing;
  const int contentHeight = pageHeight - contentTop - m.buttonHintsHeight - m.verticalSpacing * 2;

  GUI.drawList(renderer, Rect{0, contentTop, w, contentHeight}, kWeekdayPickerCount, selectorIndex,
               [](int index) { return std::string(I18N.get(kWeekdayPickerNames[index])); }, nullptr, nullptr,
               [this](int index) { return weekdayValueForIndex(index); }, false);

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}

void ClockActivity::showAlarmSoundPick() {
  const auto& m = UITheme::getInstance().getMetrics();
  const int w = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();
  GUI.drawHeader(renderer, Rect{0, m.topPadding, w, m.headerHeight}, tr(STR_CLOCK_ALARM_PICK_SOUND));

  const int contentTop = m.topPadding + m.headerHeight + m.verticalSpacing;
  const int menuBottom = pageHeight - m.buttonHintsHeight - m.verticalSpacing;
  const Rect menu{0, contentTop, w, std::max(0, menuBottom - contentTop)};
  const int count = kSoundPickerCount(musicTracks_.size());

  GUI.drawButtonMenu(
      renderer, menu, count, selectorIndex,
      [this](int i) {
        if (i == 0) {
          return std::string(tr(STR_CLOCK_ALARM_SOUND_DEFAULT));
        }
        return musicTracks_[static_cast<size_t>(i - 1)];
      },
      nullptr, UnifiedAppLayout::kMenuVisibleRows);

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}

void ClockActivity::showStopwatch() {
  const auto& m = UITheme::getInstance().getMetrics();
  const int w = renderer.getScreenWidth();
  const int headerBottom = m.topPadding + m.headerHeight;
  GUI.drawHeader(renderer, Rect{0, m.topPadding, w, m.headerHeight}, tr(STR_CLOCK_STOPWATCH));
  const auto layout = UnifiedAppLayout::splitBelowHeader(renderer, headerBottom);
  unsigned long elapsed = 0;
  if (stopwatchRunning) {
    elapsed = millis() - stopwatchStartMs;
  }
  UnifiedAppLayout::drawBigElapsedMsTile(renderer, layout.bigTile, elapsed);

  const char* line1 = stopwatchRunning ? tr(STR_CLOCK_PAUSE) : tr(STR_CLOCK_START);
  char line2[48];
  snprintf(line2, sizeof(line2), "%s: %s", tr(STR_SELECT),
           stopwatchRunning ? tr(STR_CLOCK_PAUSE) : tr(STR_CLOCK_START));
  const char* line3 = (elapsed > 0 || stopwatchRunning) ? tr(STR_CLOCK_RESET) : tr(STR_BACK);
  UnifiedAppLayout::drawMenuHintPanel(renderer, layout.menu, line1, line2, line3);

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}

void ClockActivity::showCountdown() {
  const auto& m = UITheme::getInstance().getMetrics();
  const int w = renderer.getScreenWidth();
  const int headerBottom = m.topPadding + m.headerHeight;
  GUI.drawHeader(renderer, Rect{0, m.topPadding, w, m.headerHeight}, tr(STR_CLOCK_COUNTDOWN));
  const auto layout = UnifiedAppLayout::splitBelowHeader(renderer, headerBottom);
  unsigned long leftMs = 0;
  if (countdownRunning && millis() < countdownEndMs) {
    leftMs = countdownEndMs - millis();
  } else if (countdownRunning) {
    leftMs = 0;
  }
  UnifiedAppLayout::drawBigElapsedMsTile(renderer, layout.bigTile, leftMs);

  char durVal[32];
  formatCountdownDuration(countdownSeconds, durVal, sizeof(durVal));
  char durLine[48];
  snprintf(durLine, sizeof(durLine), "%s: %s", tr(STR_CLOCK_DURATION), durVal);
  char line2[48];
  snprintf(line2, sizeof(line2), "%s: %s", tr(STR_SELECT),
           countdownRunning ? tr(STR_CLOCK_PAUSE) : tr(STR_CLOCK_START));
  const char* line3 = (leftMs > 0 || countdownRunning) ? tr(STR_CLOCK_RESET) : tr(STR_BACK);
  if (countdownRunning) {
    UnifiedAppLayout::drawMenuHintPanel(renderer, layout.menu, durLine, line2, line3);
    drawCountdownProgressBar(renderer, layout.menu, leftMs, countdownSeconds);
  } else {
    UnifiedAppLayout::drawMenuHintPanel(renderer, layout.menu, durLine, tr(STR_CLOCK_ADJUST_HINT), line2);
  }

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}

#endif
