#pragma once

#if defined(BOARD_ESP32_S3_EPAPER_397)

#include <GfxRenderer.h>
#include <cstdint>

#include "components/themes/BaseTheme.h"

namespace CalendarLayout {

constexpr int kCornerRadius = 8;
constexpr int kGridCornerRadius = 10;

struct MonthViewState {
  int viewYear = 2026;
  int viewMonth = 1;
  int selectedDay = 1;
  bool hasToday = false;
  int todayYear = 0;
  int todayMonth = 0;
  int todayDay = 0;
  bool showWeekNumbers = true;
};

struct ConvertViewState {
  bool hasToday = false;
  int todayGregYear = 0;
  int todayGregMonth = 0;
  int todayGregDay = 0;
  int todayLunarYear = 0;
  int todayLunarMonth = 0;
  int todayLunarDay = 0;
  bool todayLunarLeap = false;
  bool sourceIsGregorian = true;
  bool editing = false;
  int selectedField = 0;
  int gregYear = 2026;
  int gregMonth = 1;
  int gregDay = 1;
  int lunarYear = 2026;
  int lunarMonth = 1;
  int lunarDay = 1;
  bool lunarLeap = false;
};

void drawMonthView(const GfxRenderer& renderer, const MonthViewState& state);
void drawConvertView(const GfxRenderer& renderer, const ConvertViewState& state);

}  // namespace CalendarLayout

#endif
