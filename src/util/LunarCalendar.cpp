#include "LunarCalendar.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace LunarCalendar {
namespace {

constexpr int kMonthDayOffset[12] = {0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334};

constexpr uint32_t kLunarYearInfo[199] = {
    0x04AE53, 0x0A5748, 0x5526BD, 0x0D2650, 0x0D9544, 0x46AAB9, 0x056A4D, 0x09AD42, 0x24AEB6, 0x04AE4A, 0x6A4DBE,
    0x0A4D52, 0x0D2546, 0x5D52BA, 0x0B544E, 0x0D6A43, 0x296D37, 0x095B4B, 0x749BC1, 0x049754, 0x0A4B48, 0x5B25BC,
    0x06A550, 0x06D445, 0x4ADAB8, 0x02B64D, 0x095742, 0x2497B7, 0x04974A, 0x664B3E, 0x0D4A51, 0x0EA546, 0x56D4BA,
    0x05AD4E, 0x02B644, 0x393738, 0x092E4B, 0x7C96BF, 0x0C9553, 0x0D4A48, 0x6DA53B, 0x0B554F, 0x056A45, 0x4AADB9,
    0x025D4D, 0x092D42, 0x2C95B6, 0x0A954A, 0x7B4ABD, 0x06CA51, 0x0B5546, 0x555ABB, 0x04DA4E, 0x0A5B43, 0x352BB8,
    0x052B4C, 0x8A953F, 0x0E9552, 0x06AA48, 0x6AD53C, 0x0AB54F, 0x04B645, 0x4A5739, 0x0A574D, 0x052642, 0x3E9335,
    0x0D9549, 0x75AABE, 0x056A51, 0x096D46, 0x54AEBB, 0x04AD4F, 0x0A4D43, 0x4D26B7, 0x0D254B, 0x8D52BF, 0x0B5452,
    0x0B6A47, 0x696D3C, 0x095B50, 0x049B45, 0x4A4BB9, 0x0A4B4D, 0xAB25C2, 0x06A554, 0x06D449, 0x6ADA3D, 0x0AB651,
    0x095746, 0x5497BB, 0x04974F, 0x064B44, 0x36A537, 0x0EA54A, 0x86B2BF, 0x05AC53, 0x0AB647, 0x5936BC, 0x092E50,
    0x0C9645, 0x4D4AB8, 0x0D4A4C, 0x0DA541, 0x25AAB6, 0x056A49, 0x7AADBD, 0x025D52, 0x092D47, 0x5C95BA, 0x0A954E,
    0x0B4A43, 0x4B5537, 0x0AD54A, 0x955ABF, 0x04BA53, 0x0A5B48, 0x652BBC, 0x052B50, 0x0A9345, 0x474AB9, 0x06AA4C,
    0x0AD541, 0x24DAB6, 0x04B64A, 0x69573D, 0x0A4E51, 0x0D2646, 0x5E933A, 0x0D534D, 0x05AA43, 0x36B537, 0x096D4B,
    0xB4AEBF, 0x04AD53, 0x0A4D48, 0x6D25BC, 0x0D254F, 0x0D5244, 0x5DAA38, 0x0B5A4C, 0x056D41, 0x24ADB6, 0x049B4A,
    0x7A4BBE, 0x0A4B51, 0x0AA546, 0x5B52BA, 0x06D24E, 0x0ADA42, 0x355B37, 0x09374B, 0x8497C1, 0x049753, 0x064B48,
    0x66A53C, 0x0EA54F, 0x06B244, 0x4AB638, 0x0AAE4C, 0x092E42, 0x3C9735, 0x0C9649, 0x7D4ABD, 0x0D4A51, 0x0DA545,
    0x55AABA, 0x056A4E, 0x0A6D43, 0x452EB7, 0x052D4B, 0x8A95BF, 0x0A9553, 0x0B4A47, 0x6B553B, 0x0AD54F, 0x055A45,
    0x4A5D38, 0x0A5B4C, 0x052B42, 0x3A93B6, 0x069349, 0x7729BD, 0x06AA51, 0x0AD546, 0x54DABA, 0x04B64E, 0x0A5743,
    0x452738, 0x0D264A, 0x8E933E, 0x0D5252, 0x0DAA47, 0x66B53B, 0x056D4F, 0x04AE45, 0x4A4EB9, 0x0A4D4C, 0x0D1541,
    0x2D92B5,
};

bool isGregorianLeap(const int year) {
  return (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
}

int daysFromEpoch1900(int year, int month, int day) {
  int y = year - 1;
  int m = month + 12;
  int d = day;
  if (month > 2) {
    y = year;
    m = month;
  }
  return 365 * y + y / 4 - y / 100 + y / 400 + (153 * m - 457) / 5 + d - 306;
}

bool internalGregorianToLunar(int year, int month, int day, LunarDate& out) {
  if (year < kMinYear || year > kMaxYear || month < 1 || month > 12 || day < 1 || day > daysInGregorianMonth(year, month)) {
    return false;
  }

  const uint32_t yearInfo = kLunarYearInfo[year - 1901];
  int springNy = 0;
  if (((yearInfo & 0x0060) >> 5) == 1) {
    springNy = static_cast<int>(yearInfo & 0x001F) - 1;
  } else {
    springNy = static_cast<int>(yearInfo & 0x001F) - 1 + 31;
  }

  int sunNy = kMonthDayOffset[month - 1] + day - 1;
  if (isGregorianLeap(year) && month > 2) {
    sunNy++;
  }

  int lunarMonth = 1;
  int lunarDay = 1;
  int index = 1;
  int flag = 0;
  int monthCount = 1;
  int staticDayCount = ((yearInfo & (0x80000 >> (index - 1))) == 0) ? 29 : 30;

  if (sunNy >= springNy) {
    sunNy -= springNy;
    index = 1;
    flag = 0;
    staticDayCount = ((yearInfo & (0x80000 >> (index - 1))) == 0) ? 29 : 30;
    while (sunNy >= staticDayCount) {
      sunNy -= staticDayCount;
      index++;
      if (lunarMonth == static_cast<int>((yearInfo & 0xF00000) >> 20)) {
        flag = ~flag;
        if (flag == 0) {
          lunarMonth++;
        }
      } else {
        lunarMonth++;
      }
      staticDayCount = ((yearInfo & (0x80000 >> (index - 1))) == 0) ? 29 : 30;
      monthCount++;
    }
    lunarDay = sunNy + 1;
    out.year = year;
    out.month = lunarMonth;
    out.day = lunarDay;
    out.leapMonth = (lunarMonth == static_cast<int>((yearInfo & 0xF00000) >> 20)) && (lunarMonth != monthCount);
    return true;
  }

  springNy -= sunNy;
  year--;
  lunarMonth = 12;
  if (year >= 1901) {
    const uint32_t prevInfo = kLunarYearInfo[year - 1901];
    if (((prevInfo & 0xF00000) >> 20) == 0) {
      index = 12;
    } else {
      index = 13;
    }
    flag = 0;
    staticDayCount = ((prevInfo & (0x80000 >> (index - 1))) == 0) ? 29 : 30;
    while (springNy > staticDayCount) {
      springNy -= staticDayCount;
      index--;
      if (flag == 0) {
        lunarMonth--;
      }
      if (lunarMonth == static_cast<int>((prevInfo & 0xF00000) >> 20)) {
        flag = ~flag;
      }
      staticDayCount = ((prevInfo & (0x80000 >> (index - 1))) == 0) ? 29 : 30;
      monthCount++;
    }
    lunarDay = staticDayCount - springNy + 1;
    out.year = year;
    out.month = lunarMonth;
    out.day = lunarDay;
    out.leapMonth = (lunarMonth == static_cast<int>((prevInfo & 0xF00000) >> 20)) && (lunarMonth != monthCount);
    return true;
  }
  return false;
}

int internalIntercalaryMonth(const int lunarYear) {
  if (lunarYear < kMinYear || lunarYear > kMaxYear) {
    return 0;
  }
  return static_cast<int>((kLunarYearInfo[lunarYear - 1901] & 0xF00000) >> 20);
}

int lunarMonthToSlot(const int month, const bool leap, const int intercalary) {
  int slot = month - 1;
  if (intercalary > 0) {
    if (month > intercalary) {
      slot++;
    }
    if (month == intercalary && leap) {
      slot++;
    }
  }
  return slot;
}

void lunarSlotToMonth(const int slot, const int intercalary, int& month, bool& leap) {
  leap = false;
  if (intercalary <= 0) {
    month = slot + 1;
    return;
  }
  if (slot < intercalary) {
    month = slot + 1;
  } else if (slot == intercalary) {
    month = intercalary;
    leap = true;
  } else {
    month = slot;
  }
}

}  // namespace

int daysInGregorianMonth(const int year, const int month) {
  static constexpr int kDays[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  if (month < 1 || month > 12) {
    return 30;
  }
  if (month == 2 && isGregorianLeap(year)) {
    return 29;
  }
  return kDays[month - 1];
}

int weekdaySun0(const int year, const int month, const int day) {
  int y = year;
  int m = month;
  if (m < 3) {
    m += 12;
    y--;
  }
  const int w = (y + y / 4 - y / 100 + y / 400 + (153 * m - 457) / 5 + day - 306) % 7;
  return (w + 7) % 7;
}

int weekdayMon0(const int year, const int month, const int day) {
  return (weekdaySun0(year, month, day) + 6) % 7;
}

bool addGregorianDays(int& year, int& month, int& day, int delta) {
  if (delta == 0) {
    return year >= kMinYear && year <= kMaxYear;
  }

  int remaining = delta > 0 ? delta : -delta;
  const int step = delta > 0 ? 1 : -1;
  while (remaining > 0) {
    day += step;
    if (step > 0) {
      if (day > daysInGregorianMonth(year, month)) {
        day = 1;
        month++;
        if (month > 12) {
          month = 1;
          year++;
        }
      }
    } else {
      if (day < 1) {
        month--;
        if (month < 1) {
          month = 12;
          year--;
        }
        day = daysInGregorianMonth(year, month);
      }
    }
    remaining--;
  }
  return year >= kMinYear && year <= kMaxYear;
}

namespace {

int dayOfYear(const int year, const int month, const int day) {
  static constexpr int kMonthStart[12] = {0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334};
  int doy = kMonthStart[month - 1] + day;
  if (month > 2 && isGregorianLeap(year)) {
    doy++;
  }
  return doy;
}

int isoWeekRawFromThursday(const int y, const int m, const int d) {
  const int doy = dayOfYear(y, m, d);
  const int jan4Mon0 = weekdayMon0(y, 1, 4);
  return (doy - (4 - jan4Mon0)) / 7 + 1;
}

int isoWeeksInGregorianYear(const int gregYear) {
  int y = gregYear;
  int m = 12;
  int d = 28;
  const int toThu = (4 - weekdaySun0(y, m, d) + 7) % 7;
  if (!addGregorianDays(y, m, d, toThu)) {
    return 52;
  }
  return isoWeekRawFromThursday(y, m, d);
}

}  // namespace

bool isoWeekAndYear(const int year, const int month, const int day, int& isoYear, int& isoWeek) {
  int y = year;
  int m = month;
  int d = day;
  const int toThu = (4 - weekdaySun0(y, m, d) + 7) % 7;
  if (!addGregorianDays(y, m, d, toThu)) {
    return false;
  }

  isoWeek = isoWeekRawFromThursday(y, m, d);
  isoYear = y;

  if (isoWeek < 1) {
    isoYear = y - 1;
    isoWeek = isoWeeksInGregorianYear(isoYear);
  } else {
    const int weeksInYear = isoWeeksInGregorianYear(y);
    if (isoWeek > weeksInYear) {
      isoYear = y + 1;
      isoWeek = 1;
    }
  }
  return true;
}

int isoWeekForMonthGridRow(const int year, const int month, const int gridRow) {
  const int firstMon0 = weekdayMon0(year, month, 1);
  const int mondayDay = 1 - firstMon0 + gridRow * 7;
  int y = year;
  int m = month;
  int d = 1;
  if (!addGregorianDays(y, m, d, mondayDay - 1 + 3)) {
    return 0;
  }
  int isoYear = 0;
  int isoWeek = 0;
  if (!isoWeekAndYear(y, m, d, isoYear, isoWeek)) {
    return 0;
  }
  return isoWeek;
}

bool gregorianToLunar(const int year, const int month, const int day, LunarDate& out) {
  return internalGregorianToLunar(year, month, day, out);
}

bool lunarToGregorian(const int lunarYear, const int lunarMonth, const int lunarDay, const bool leapMonth, int& outYear,
                      int& outMonth, int& outDay) {
  if (lunarYear < kMinYear || lunarYear > kMaxYear || lunarMonth < 1 || lunarMonth > 12 || lunarDay < 1) {
    return false;
  }
  const int y0 = lunarYear - 1;
  const int y1 = lunarYear + 1;
  for (int y = y0; y <= y1; ++y) {
    for (int m = 1; m <= 12; ++m) {
      const int dim = daysInGregorianMonth(y, m);
      for (int d = 1; d <= dim; ++d) {
        LunarDate lunar{};
        if (!internalGregorianToLunar(y, m, d, lunar)) {
          continue;
        }
        if (lunar.year == lunarYear && lunar.month == lunarMonth && lunar.day == lunarDay &&
            lunar.leapMonth == leapMonth) {
          outYear = y;
          outMonth = m;
          outDay = d;
          return true;
        }
      }
    }
  }
  return false;
}

int daysInLunarMonth(const int lunarYear, const int lunarMonth, const bool leapMonth) {
  if (lunarYear < kMinYear || lunarYear > kMaxYear || lunarMonth < 1 || lunarMonth > 12) {
    return 30;
  }
  for (int day = 30; day >= 29; --day) {
    int y = 0;
    int m = 0;
    int d = 0;
    if (lunarToGregorian(lunarYear, lunarMonth, day, leapMonth, y, m, d)) {
      return day;
    }
  }
  return 30;
}

int intercalaryMonth(const int lunarYear) {
  return internalIntercalaryMonth(lunarYear);
}

int lunarMonthSlotCount(const int lunarYear) {
  return internalIntercalaryMonth(lunarYear) > 0 ? 13 : 12;
}

void normalizeLunarMonthLeap(const int lunarYear, int& month, bool& leap) {
  month = std::clamp(month, 1, 12);
  const int intercalary = internalIntercalaryMonth(lunarYear);
  if (intercalary == 0) {
    leap = false;
  } else if (leap && month != intercalary) {
    leap = false;
  }
}

void bumpLunarMonth(int& lunarYear, int& month, bool& leap, const int delta) {
  if (delta == 0) {
    return;
  }

  int year = lunarYear;
  normalizeLunarMonthLeap(year, month, leap);

  int intercalary = internalIntercalaryMonth(year);
  int slotCount = intercalary > 0 ? 13 : 12;
  int slot = lunarMonthToSlot(month, leap, intercalary);
  slot += delta;

  while (slot < 0) {
    year--;
    if (year < kMinYear) {
      year = kMinYear;
      intercalary = internalIntercalaryMonth(year);
      slot = 0;
      break;
    }
    intercalary = internalIntercalaryMonth(year);
    slotCount = intercalary > 0 ? 13 : 12;
    slot += slotCount;
  }

  while (slot >= slotCount) {
    year++;
    if (year > kMaxYear) {
      year = kMaxYear;
      intercalary = internalIntercalaryMonth(year);
      slotCount = intercalary > 0 ? 13 : 12;
      slot = slotCount - 1;
      break;
    }
    slot -= slotCount;
    intercalary = internalIntercalaryMonth(year);
    slotCount = intercalary > 0 ? 13 : 12;
  }

  intercalary = internalIntercalaryMonth(year);
  lunarSlotToMonth(slot, intercalary, month, leap);
  lunarYear = year;
}

bool resolveLunarToGregorian(const int lunarYear, const int lunarMonth, int& lunarDay, bool& leapMonth, int& outYear,
                             int& outMonth, int& outDay) {
  if (lunarYear < kMinYear || lunarYear > kMaxYear || lunarMonth < 1 || lunarMonth > 12) {
    return false;
  }

  lunarDay = std::clamp(lunarDay, 1, 30);

  auto tryConvert = [&](const int day, const bool leap) {
    return lunarToGregorian(lunarYear, lunarMonth, day, leap, outYear, outMonth, outDay);
  };

  if (tryConvert(lunarDay, leapMonth)) {
    return true;
  }
  if (tryConvert(lunarDay, !leapMonth)) {
    leapMonth = !leapMonth;
    return true;
  }

  const int maxDay = std::max(daysInLunarMonth(lunarYear, lunarMonth, leapMonth),
                              daysInLunarMonth(lunarYear, lunarMonth, !leapMonth));
  for (int day = std::min(lunarDay, maxDay); day >= 1; --day) {
    if (tryConvert(day, leapMonth)) {
      lunarDay = day;
      return true;
    }
    if (tryConvert(day, !leapMonth)) {
      lunarDay = day;
      leapMonth = !leapMonth;
      return true;
    }
  }
  return false;
}

void formatLunar(const LunarDate& lunar, char* buf, const size_t bufSize) {
  if (buf == nullptr || bufSize == 0) {
    return;
  }
  if (lunar.leapMonth) {
    snprintf(buf, bufSize, "L%02d-%02d-%02d", lunar.year, lunar.month, lunar.day);
  } else {
    snprintf(buf, bufSize, "%04d-%02d-%02d", lunar.year, lunar.month, lunar.day);
  }
}

void formatGregorian(const int year, const int month, const int day, char* buf, const size_t bufSize) {
  if (buf == nullptr || bufSize == 0) {
    return;
  }
  snprintf(buf, bufSize, "%04d-%02d-%02d", year, month, day);
}

}  // namespace LunarCalendar
