#include "ClockSettingsActivity.h"

#if defined(BOARD_ESP32_S3_EPAPER_397)

#include <HalBoard397.h>
#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>
#include <cstdio>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/UnifiedAppLayout.h"

namespace {

constexpr int kMenuItems = 7;

constexpr StrId menuNames[kMenuItems] = {
    StrId::STR_CLOCK_YEAR,     StrId::STR_CLOCK_MONTH, StrId::STR_CLOCK_DAY, StrId::STR_CLOCK_HOUR,
    StrId::STR_CLOCK_MINUTE,   StrId::STR_CLOCK_TIMEZONE, StrId::STR_CLOCK_DST};

uint8_t daysInMonth(const uint16_t y, const uint8_t m) {
  static constexpr uint8_t days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  if (m < 1 || m > 12) {
    return 31;
  }
  uint8_t d = days[m - 1];
  if (m == 2) {
    const bool leap = (y % 4 == 0 && y % 100 != 0) || (y % 400 == 0);
    if (leap) {
      d = 29;
    }
  }
  return d;
}

}  // namespace

ClockSettingsActivity::ClockSettingsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : Activity("ClockSettings", renderer, mappedInput) {}

void ClockSettingsActivity::loadFromRtc() {
  timezoneOffsetMinutes = SETTINGS.timezoneOffsetMinutes;
  dstEnabled = SETTINGS.dstEnabled;
  HalBoard397::DateTime rtc{};
  if (board397.readRtcForDisplay(rtc)) {
    year = rtc.year;
    month = rtc.month;
    day = rtc.day;
    hour = rtc.hour;
    minute = rtc.minute;
  } else {
    year = 2026;
    month = 1;
    day = 1;
    hour = 12;
    minute = 0;
  }
}

void ClockSettingsActivity::saveToRtc() {
  const uint8_t maxDay = daysInMonth(year, month);
  if (day > maxDay) {
    day = maxDay;
  }
  if (day < 1) {
    day = 1;
  }

  HalBoard397::DateTime dt{};
  dt.year = year;
  dt.month = month;
  dt.day = day;
  dt.hour = hour;
  dt.minute = minute;
  dt.second = 0;

  if (board397.hasRtc()) {
    board397.setRtc(dt);
  }

  SETTINGS.timezoneOffsetMinutes = timezoneOffsetMinutes;
  SETTINGS.dstEnabled = dstEnabled > 0 ? 1 : 0;
  SETTINGS.applyTimezoneToSystem();
  SETTINGS.saveToFile();
  dirty = false;
}

void ClockSettingsActivity::adjustField(const int delta, const int step) {
  if (delta == 0 || step <= 0) {
    return;
  }
  dirty = true;
  const int change = delta * step;

  switch (selectedIndex) {
    case 0: {
      int y = static_cast<int>(year) + change;
      year = static_cast<uint16_t>(std::clamp(y, 2020, 2099));
      break;
    }
    case 1: {
      int m = static_cast<int>(month) + change;
      while (m < 1) {
        m += 12;
      }
      while (m > 12) {
        m -= 12;
      }
      month = static_cast<uint8_t>(m);
      break;
    }
    case 2: {
      const uint8_t maxDay = daysInMonth(year, month);
      int d = static_cast<int>(day) + change;
      while (d < 1) {
        d += maxDay;
      }
      while (d > maxDay) {
        d -= maxDay;
      }
      day = static_cast<uint8_t>(d);
      break;
    }
    case 3:
      hour = static_cast<uint8_t>((static_cast<int>(hour) + change % 24 + 24) % 24);
      break;
    case 4:
      minute = static_cast<uint8_t>((static_cast<int>(minute) + change % 60 + 60) % 60);
      break;
    case 5: {
      // One step = 30 minutes offset.
      int tz = static_cast<int>(timezoneOffsetMinutes) + change * 30;
      if (tz > 840) {
        tz = 840;
      }
      if (tz < -720) {
        tz = -720;
      }
      timezoneOffsetMinutes = static_cast<int16_t>(tz);
      break;
    }
    case 6:
      if (change > 0) {
        dstEnabled = 1;
      } else if (change < 0) {
        dstEnabled = 0;
      }
      break;
    default:
      break;
  }

  if (selectedIndex != 2) {
    const uint8_t maxDay = daysInMonth(year, month);
    if (day > maxDay) {
      day = maxDay;
    }
  }
}

int ClockSettingsActivity::stepForRepeat(const int repeatCount) const {
  if (selectedIndex == 6) {
    return 1;
  }

  int step = 5 + (repeatCount / 3) * 5;

  int maxStep = 60;
  switch (selectedIndex) {
    case 0:
      maxStep = 20;
      break;
    case 1:
      maxStep = 6;
      break;
    case 2:
      maxStep = 15;
      break;
    case 3:
      maxStep = 12;
      break;
    case 4:
      maxStep = 30;
      break;
    case 5:
      maxStep = 24;
      break;
    default:
      break;
  }

  return std::min(step, maxStep);
}

unsigned long ClockSettingsActivity::intervalForRepeat(const int repeatCount) const {
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

void ClockSettingsActivity::pollHoldAdjust(const MappedInputManager::Button button, const int direction,
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
      adjustField(direction, 1);
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
  adjustField(direction, step);
  state.didRepeat = true;
  state.repeatCount++;
  state.lastRepeatMs = now;
  requestUpdate();
}

void ClockSettingsActivity::loopBrowse() {
  buttonNavigator.onNextRelease([this] {
    selectedIndex = ButtonNavigator::nextIndex(selectedIndex, kMenuItems);
    requestUpdate();
  });
  buttonNavigator.onPreviousRelease([this] {
    selectedIndex = ButtonNavigator::previousIndex(selectedIndex, kMenuItems);
    requestUpdate();
  });
}

void ClockSettingsActivity::loopEdit() {
  pollHoldAdjust(MappedInputManager::Button::Up, +1, upHold_);
  pollHoldAdjust(MappedInputManager::Button::Down, -1, downHold_);
}

std::string ClockSettingsActivity::formatTimezone() const {
  const int sign = timezoneOffsetMinutes < 0 ? -1 : 1;
  const int total = timezoneOffsetMinutes * sign;
  const int hours = total / 60;
  const int mins = total % 60;
  char buf[16];
  snprintf(buf, sizeof(buf), tr(STR_UTC_OFFSET), sign * hours, mins);
  return buf;
}

std::string ClockSettingsActivity::valueForIndex(const int index) const {
  switch (index) {
    case 0:
      return std::to_string(year);
    case 1:
      return std::to_string(month);
    case 2:
      return std::to_string(day);
    case 3:
      return std::to_string(hour);
    case 4:
      return std::to_string(minute);
    case 5:
      return formatTimezone();
    case 6:
      return std::string(dstEnabled != 0 ? tr(STR_STATE_ON) : tr(STR_STATE_OFF));
    default:
      return {};
  }
}

std::string ClockSettingsActivity::listRowValue(const int index) const { return valueForIndex(index); }

void ClockSettingsActivity::pollSecondRefresh() {
  if (UnifiedAppLayout::pollRtcSecondTick(lastRtcSecond)) {
    requestUpdate();
  }
}

void ClockSettingsActivity::onEnter() {
  Activity::onEnter();
  selectedIndex = 0;
  editing = false;
  upHold_ = {};
  downHold_ = {};
  loadFromRtc();
  dirty = false;
  lastRtcSecond = 255;
  requestUpdate();
}

void ClockSettingsActivity::onExit() {
  if (dirty) {
    saveToRtc();
  }
  Activity::onExit();
}

void ClockSettingsActivity::loop() {
  pollSecondRefresh();

  if (mappedInput.wasBackClicked()) {
    if (editing) {
      editing = false;
      upHold_ = {};
      downHold_ = {};
      requestUpdate();
      return;
    }
    if (dirty) {
      saveToRtc();
    }
    finish();
    return;
  }

  if (consumeConfirmClick()) {
    editing = !editing;
    upHold_ = {};
    downHold_ = {};
    requestUpdate();
    return;
  }

  if (editing) {
    loopEdit();
  } else {
    loopBrowse();
  }
}

void ClockSettingsActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto pageWidth = renderer.getScreenWidth();
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int headerBottom = metrics.topPadding + metrics.headerHeight;

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_CLOCK_SETTINGS));

  const auto layout =
      UnifiedAppLayout::splitBelowHeader(renderer, headerBottom, UnifiedAppLayout::kCompactBigTileHeight);
  UnifiedAppLayout::drawBigRtcClockTile(renderer, layout.bigTile);
  UnifiedAppLayout::drawRtcDateCaptionInTile(renderer, layout.bigTile);

  GUI.drawList(renderer, layout.menu, kMenuItems, selectedIndex,
               [](int index) { return std::string(I18N.get(menuNames[index])); }, nullptr, nullptr,
               [this](int index) { return listRowValue(index); }, editing);

  MappedInputManager::Labels labels;
  if (editing) {
    labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_DONE), "+", "-");
  } else {
    labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_CLOCK_EDIT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  }
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}

#endif
