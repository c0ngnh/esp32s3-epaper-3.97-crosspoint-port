#include "CalendarActivity.h"

#if defined(BOARD_ESP32_S3_EPAPER_397)

#include <HalBoard397.h>
#include <HalTiltSensor.h>
#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>
#include <cstdio>
#include <string>

#include "MappedInputManager.h"
#include "activities/reader/ReaderUtils.h"
#include "components/UITheme.h"
#include "util/CalendarLayout.h"
#include "util/LunarCalendar.h"

namespace {

void clampGregorian(int& y, int& m, int& d) {
  y = std::clamp(y, LunarCalendar::kMinYear, LunarCalendar::kMaxYear);
  m = std::clamp(m, 1, 12);
  d = std::clamp(d, 1, LunarCalendar::daysInGregorianMonth(y, m));
}

}  // namespace

void CalendarActivity::syncLunarFromGregorian() {
  LunarDate lunar{};
  if (LunarCalendar::gregorianToLunar(gregYear, gregMonth, gregDay, lunar)) {
    lunarYear = lunar.year;
    lunarMonth = lunar.month;
    lunarDay = lunar.day;
    lunarLeap = lunar.leapMonth;
  }
}

void CalendarActivity::syncGregorianFromLunar() {
  int y = gregYear;
  int m = gregMonth;
  int d = gregDay;
  if (LunarCalendar::lunarToGregorian(lunarYear, lunarMonth, lunarDay, lunarLeap, y, m, d)) {
    gregYear = y;
    gregMonth = m;
    gregDay = d;
  }
}

void CalendarActivity::clampSelectedDayInViewMonth() {
  const int dim = LunarCalendar::daysInGregorianMonth(viewYear, viewMonth);
  if (selectedDay > dim) {
    selectedDay = dim;
  }
  if (selectedDay < 1) {
    selectedDay = 1;
  }
}

void CalendarActivity::bumpViewMonth(const int delta) {
  if (delta == 0) {
    return;
  }
  int m = viewMonth + delta;
  int y = viewYear;
  if (m < 1) {
    m = 12;
    y--;
  } else if (m > 12) {
    m = 1;
    y++;
  }
  viewYear = std::clamp(y, LunarCalendar::kMinYear, LunarCalendar::kMaxYear);
  viewMonth = m;
  clampSelectedDayInViewMonth();
}

void CalendarActivity::moveSelectedDay(const int delta) {
  if (delta == 0) {
    return;
  }

  int y = viewYear;
  int m = viewMonth;
  int d = selectedDay + delta;

  while (d < 1) {
    if (m == 1) {
      m = 12;
      y--;
    } else {
      m--;
    }
    y = std::clamp(y, LunarCalendar::kMinYear, LunarCalendar::kMaxYear);
    d += LunarCalendar::daysInGregorianMonth(y, m);
  }

  while (d > LunarCalendar::daysInGregorianMonth(y, m)) {
    d -= LunarCalendar::daysInGregorianMonth(y, m);
    if (m == 12) {
      m = 1;
      y++;
    } else {
      m++;
    }
    y = std::clamp(y, LunarCalendar::kMinYear, LunarCalendar::kMaxYear);
  }

  viewYear = y;
  viewMonth = m;
  selectedDay = d;
}

void CalendarActivity::moveSelectedDayByWeeks(const int weeks) {
  if (weeks == 0) {
    return;
  }
  moveSelectedDay(weeks * 7);
}

void CalendarActivity::pollMonthTiltNav() {
  if (!halTiltSensor.isAvailable()) {
    return;
  }
  halTiltSensor.updateKeyboardNav();
  switch (halTiltSensor.consumeKeyboardTilt()) {
    case HalTiltSensor::KeyboardTiltDir::Left:
      moveSelectedDay(-1);
      requestUpdate();
      break;
    case HalTiltSensor::KeyboardTiltDir::Right:
      moveSelectedDay(1);
      requestUpdate();
      break;
    case HalTiltSensor::KeyboardTiltDir::Up:
      moveSelectedDayByWeeks(1);
      requestUpdate();
      break;
    case HalTiltSensor::KeyboardTiltDir::Down:
      moveSelectedDayByWeeks(-1);
      requestUpdate();
      break;
    default:
      break;
  }
}

void CalendarActivity::adjustSourceField(const int delta, const int step) {
  if (delta == 0 || step <= 0) {
    return;
  }

  if (convertField == 0) {
    convertSourceGregorian = delta > 0;
    if (convertSourceGregorian) {
      syncLunarFromGregorian();
    } else {
      syncGregorianFromLunar();
      clampGregorian(gregYear, gregMonth, gregDay);
    }
    return;
  }

  const int change = delta * step;

  if (convertSourceGregorian) {
    switch (convertField) {
      case 1:
        gregYear += change;
        break;
      case 2:
        gregMonth += change;
        while (gregMonth < 1) {
          gregMonth += 12;
          gregYear--;
        }
        while (gregMonth > 12) {
          gregMonth -= 12;
          gregYear++;
        }
        break;
      case 3: {
        const int maxDay = LunarCalendar::daysInGregorianMonth(gregYear, gregMonth);
        int d = gregDay + change;
        while (d < 1) {
          d += maxDay;
        }
        while (d > maxDay) {
          d -= maxDay;
        }
        gregDay = d;
        break;
      }
      default:
        return;
    }
    clampGregorian(gregYear, gregMonth, gregDay);
    syncLunarFromGregorian();
  } else {
    switch (convertField) {
      case 1:
        lunarYear += change;
        break;
      case 2:
        lunarMonth += change;
        while (lunarMonth < 1) {
          lunarMonth += 12;
          lunarYear--;
        }
        while (lunarMonth > 12) {
          lunarMonth -= 12;
          lunarYear++;
        }
        break;
      case 3: {
        int d = lunarDay + change;
        while (d < 1) {
          d += 30;
        }
        while (d > 30) {
          d -= 30;
        }
        lunarDay = d;
        break;
      }
      default:
        return;
    }
    lunarYear = std::clamp(lunarYear, LunarCalendar::kMinYear, LunarCalendar::kMaxYear);
    lunarMonth = std::clamp(lunarMonth, 1, 12);
    lunarDay = std::clamp(lunarDay, 1, 30);
    syncGregorianFromLunar();
    clampGregorian(gregYear, gregMonth, gregDay);
  }
}

int CalendarActivity::stepForRepeat(const int repeatCount) const {
  if (convertField == 0) {
    return 1;
  }
  int step = 5 + (repeatCount / 3) * 5;
  int maxStep = 60;
  switch (convertField) {
    case 1:
      maxStep = 20;
      break;
    case 2:
      maxStep = 6;
      break;
    case 3:
      maxStep = 15;
      break;
    default:
      break;
  }
  return std::min(step, maxStep);
}

unsigned long CalendarActivity::intervalForRepeat(const int repeatCount) const {
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

void CalendarActivity::pollHoldAdjust(const MappedInputManager::Button button, const int direction,
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
      adjustSourceField(direction, 1);
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
  adjustSourceField(direction, step);
  state.didRepeat = true;
  state.repeatCount++;
  state.lastRepeatMs = now;
  requestUpdate();
}

void CalendarActivity::loopConvertBrowse() {
  buttonNavigator.onNextRelease([this] {
    convertField = ButtonNavigator::nextIndex(convertField, kConvertMenuItems);
    requestUpdate();
  });
  buttonNavigator.onPreviousRelease([this] {
    convertField = ButtonNavigator::previousIndex(convertField, kConvertMenuItems);
    requestUpdate();
  });
}

void CalendarActivity::loopConvertEdit() {
  pollHoldAdjust(MappedInputManager::Button::Up, +1, upHold_);
  pollHoldAdjust(MappedInputManager::Button::Down, -1, downHold_);
}

void CalendarActivity::loopConvert() {
  if (mappedInput.wasBackClicked()) {
    if (convertEditing) {
      convertEditing = false;
      upHold_ = {};
      downHold_ = {};
      requestUpdate();
      return;
    }
    viewYear = gregYear;
    viewMonth = gregMonth;
    selectedDay = gregDay;
    clampSelectedDayInViewMonth();
    mode = Mode::Month;
    if (halTiltSensor.isAvailable()) {
      halTiltSensor.setKeyboardNavActive(true);
    }
    requestUpdate();
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    if (!convertEditing && convertField == 0) {
      convertSourceGregorian = !convertSourceGregorian;
      if (convertSourceGregorian) {
        syncLunarFromGregorian();
      } else {
        syncGregorianFromLunar();
        clampGregorian(gregYear, gregMonth, gregDay);
      }
      requestUpdate();
      return;
    }
    convertEditing = !convertEditing;
    upHold_ = {};
    downHold_ = {};
    requestUpdate();
    return;
  }

  if (convertEditing) {
    loopConvertEdit();
  } else {
    loopConvertBrowse();
  }
}

bool CalendarActivity::isViewingToday() const {
  HalBoard397::DateTime today{};
  if (!board397.readRtcForDisplay(today)) {
    return false;
  }
  return viewYear == today.year && viewMonth == today.month && selectedDay == today.day;
}

void CalendarActivity::goToToday() {
  HalBoard397::DateTime today{};
  if (!board397.readRtcForDisplay(today)) {
    return;
  }
  viewYear = today.year;
  viewMonth = today.month;
  selectedDay = today.day;
}

void CalendarActivity::loopMonthView() {
  if (halTiltSensor.isAvailable()) {
    pollMonthTiltNav();
    if (mappedInput.wasReleased(MappedInputManager::Button::Up)) {
      bumpViewMonth(-1);
      requestUpdate();
    } else if (mappedInput.wasReleased(MappedInputManager::Button::Down)) {
      bumpViewMonth(1);
      requestUpdate();
    }
  } else {
    if (mappedInput.wasReleased(MappedInputManager::Button::Up)) {
      moveSelectedDayByWeeks(-1);
      requestUpdate();
    } else if (mappedInput.wasReleased(MappedInputManager::Button::Down)) {
      moveSelectedDayByWeeks(1);
      requestUpdate();
    } else if (mappedInput.wasReleased(MappedInputManager::Button::Left)) {
      moveSelectedDay(-1);
      requestUpdate();
    } else if (mappedInput.wasReleased(MappedInputManager::Button::Right)) {
      moveSelectedDay(1);
      requestUpdate();
    }
  }
}

void CalendarActivity::onEnter() {
  Activity::onEnter();
  HalBoard397::DateTime dt{};
  if (board397.readRtcForDisplay(dt)) {
    viewYear = dt.year;
    viewMonth = dt.month;
    selectedDay = dt.day;
    gregYear = dt.year;
    gregMonth = dt.month;
    gregDay = dt.day;
    syncLunarFromGregorian();
  }
  mode = Mode::Month;
  convertSourceGregorian = true;
  convertEditing = false;
  convertField = 0;
  upHold_ = {};
  downHold_ = {};
  if (halTiltSensor.isAvailable()) {
    halTiltSensor.setKeyboardNavActive(true);
  }
  requestUpdate();
}

void CalendarActivity::onExit() {
  halTiltSensor.setKeyboardNavActive(false);
  halTiltSensor.clearKeyboardTilt();
  Activity::onExit();
}

void CalendarActivity::loop() {
  if (mode == Mode::Convert) {
    loopConvert();
    return;
  }

  loopMonthView();

  if (ReaderUtils::wasShortBackClicked(mappedInput)) {
    HalBoard397::DateTime today{};
    if (!board397.readRtcForDisplay(today)) {
      finish();
      return;
    }
    if (isViewingToday()) {
      finish();
    } else {
      goToToday();
      requestUpdate();
    }
    return;
  }

  if (consumeConfirmClick()) {
    gregYear = viewYear;
    gregMonth = viewMonth;
    gregDay = selectedDay;
    clampGregorian(gregYear, gregMonth, gregDay);
    syncLunarFromGregorian();
    mode = Mode::Convert;
    convertSourceGregorian = true;
    convertEditing = false;
    convertField = 1;
    upHold_ = {};
    downHold_ = {};
    if (halTiltSensor.isAvailable()) {
      halTiltSensor.setKeyboardNavActive(false);
    }
    requestUpdate();
  }
}

void CalendarActivity::showMonth() {
  HalBoard397::DateTime today{};
  CalendarLayout::MonthViewState state{};
  state.viewYear = viewYear;
  state.viewMonth = viewMonth;
  state.selectedDay = selectedDay;
  state.hasToday = board397.readRtcForDisplay(today);
  if (state.hasToday) {
    state.todayYear = today.year;
    state.todayMonth = today.month;
    state.todayDay = today.day;
  }
  CalendarLayout::drawMonthView(renderer, state);

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_CALENDAR_CONVERT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}

void CalendarActivity::showConvert() {
  HalBoard397::DateTime today{};
  CalendarLayout::ConvertViewState state{};
  state.hasToday = board397.readRtcForDisplay(today);
  if (state.hasToday) {
    state.todayGregYear = today.year;
    state.todayGregMonth = today.month;
    state.todayGregDay = today.day;
    LunarDate todayLunar{};
    if (LunarCalendar::gregorianToLunar(today.year, today.month, today.day, todayLunar)) {
      state.todayLunarYear = todayLunar.year;
      state.todayLunarMonth = todayLunar.month;
      state.todayLunarDay = todayLunar.day;
      state.todayLunarLeap = todayLunar.leapMonth;
    }
  }
  state.sourceIsGregorian = convertSourceGregorian;
  state.editing = convertEditing;
  state.selectedField = convertField;
  state.gregYear = gregYear;
  state.gregMonth = gregMonth;
  state.gregDay = gregDay;
  state.lunarYear = lunarYear;
  state.lunarMonth = lunarMonth;
  state.lunarDay = lunarDay;
  state.lunarLeap = lunarLeap;

  CalendarLayout::drawConvertView(renderer, state);

  MappedInputManager::Labels labels;
  if (convertEditing) {
    labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_DONE), "+", "-");
  } else {
    labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_CLOCK_EDIT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  }
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}

void CalendarActivity::render(RenderLock&&) {
  renderer.clearScreen();
  if (mode == Mode::Convert) {
    showConvert();
  } else {
    showMonth();
  }
  renderer.displayBuffer();
}

#endif
