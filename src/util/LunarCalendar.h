#pragma once

#include <cstddef>
#include <cstdint>

struct LunarDate {
  int year = 0;
  int month = 0;
  int day = 0;
  bool leapMonth = false;
};

namespace LunarCalendar {

constexpr int kMinYear = 1901;
constexpr int kMaxYear = 2099;

int daysInGregorianMonth(int year, int month);
int weekdaySun0(int year, int month, int day);

bool gregorianToLunar(int year, int month, int day, LunarDate& out);
bool lunarToGregorian(int lunarYear, int lunarMonth, int lunarDay, bool leapMonth, int& outYear, int& outMonth,
                      int& outDay);

// Days in a lunar month (29 or 30). Uses leapMonth when the year has an intercalary month.
int daysInLunarMonth(int lunarYear, int lunarMonth, bool leapMonth);

// Intercalary month number (1-12) for a lunar year, or 0 if the year has no leap month.
int intercalaryMonth(int lunarYear);

// Month slots in a lunar year: 12 normally, 13 when an intercalary month is present.
int lunarMonthSlotCount(int lunarYear);

// Clear an invalid leap flag (e.g. after changing year or month without stepping).
void normalizeLunarMonthLeap(int lunarYear, int& month, bool& leap);

// Step the lunar month field by delta, visiting the intercalary month when present (7->8->8L->9).
void bumpLunarMonth(int& lunarYear, int& month, bool& leap, int delta);

// Like lunarToGregorian but toggles leapMonth and clamps day when needed so UI edits stay in sync.
bool resolveLunarToGregorian(int lunarYear, int lunarMonth, int& lunarDay, bool& leapMonth, int& outYear,
                             int& outMonth, int& outDay);

void formatLunar(const LunarDate& lunar, char* buf, size_t bufSize);
void formatGregorian(int year, int month, int day, char* buf, size_t bufSize);

}  // namespace LunarCalendar
