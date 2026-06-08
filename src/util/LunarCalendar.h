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

void formatLunar(const LunarDate& lunar, char* buf, size_t bufSize);
void formatGregorian(int year, int month, int day, char* buf, size_t bufSize);

}  // namespace LunarCalendar
