#include "CalendarLayout.h"

#if defined(BOARD_ESP32_S3_EPAPER_397)

#include <I18n.h>

#include <algorithm>
#include <cstdio>
#include <string>

#include "components/UITheme.h"
#include "fontIds.h"
#include "util/LunarCalendar.h"

namespace CalendarLayout {
namespace {

constexpr int kWeekdayLabelCount = 7;
constexpr const char* kWeekdayLabels[kWeekdayLabelCount] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};

constexpr const char* kMonthNames[13] = {"",     "January", "February", "March",  "April",   "May",      "June",
                                         "July", "August",  "September", "October", "November", "December"};
constexpr const char* kMonthAbbrevs[13] = {"",    "Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                           "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

constexpr StrId kFieldLabelIds[3] = {StrId::STR_YEAR, StrId::STR_MONTH, StrId::STR_DAY};

StrId weekdayStrId(const int sun0Index) {
  static constexpr StrId kIds[7] = {StrId::STR_WEEKDAY_SUN, StrId::STR_WEEKDAY_MON, StrId::STR_WEEKDAY_TUE,
                                    StrId::STR_WEEKDAY_WED, StrId::STR_WEEKDAY_THU, StrId::STR_WEEKDAY_FRI,
                                    StrId::STR_WEEKDAY_SAT};
  if (sun0Index >= 0 && sun0Index < 7) {
    return kIds[sun0Index];
  }
  return StrId::STR_WEEKDAY_SUN;
}

const char* monthName(const int month) {
  if (month >= 1 && month <= 12) {
    return kMonthNames[month];
  }
  return "?";
}

const char* monthAbbrev(const int month) {
  if (month >= 1 && month <= 12) {
    return kMonthAbbrevs[month];
  }
  return "???";
}

void formatGregorianMmmDdYyyy(const int year, const int month, const int day, char* buf, const size_t bufSize) {
  snprintf(buf, bufSize, "%s %02d, %d", monthAbbrev(month), day, year);
}

void formatLunarMmmDdYyyy(const LunarDate& lunar, char* buf, const size_t bufSize) {
  if (lunar.leapMonth) {
    snprintf(buf, bufSize, "%s L %02d, %d", monthAbbrev(lunar.month), lunar.day, lunar.year);
  } else {
    snprintf(buf, bufSize, "%s %02d, %d", monthAbbrev(lunar.month), lunar.day, lunar.year);
  }
}

const char* weekdayName(const int sun0Index) {
  static const char* kFull[] = {"Sunday",   "Monday", "Tuesday", "Wednesday",
                                "Thursday", "Friday", "Saturday"};
  if (sun0Index >= 0 && sun0Index < 7) {
    return kFull[sun0Index];
  }
  return "";
}

void drawPanel(const GfxRenderer& renderer, const Rect& rect, const Color fill) {
  renderer.fillRoundedRect(rect.x, rect.y, rect.width, rect.height, kCornerRadius, fill);
  renderer.drawRoundedRect(rect.x, rect.y, rect.width, rect.height, 1, kCornerRadius, true);
}

void drawCenteredInCell(const GfxRenderer& renderer, const Rect& cell, const int fontId, const char* text,
                        const bool whiteText, const EpdFontFamily::Style style) {
  if (text == nullptr || text[0] == '\0' || cell.width <= 0 || cell.height <= 0) {
    return;
  }
  const std::string fitted = renderer.truncatedText(fontId, text, cell.width, style);
  const int tw = renderer.getTextWidth(fontId, fitted.c_str(), style);
  const int lh = renderer.getLineHeight(fontId);
  int tx = cell.x + (cell.width - tw) / 2;
  const int ty = cell.y + (cell.height - lh) / 2;
  if (tx < cell.x) {
    tx = cell.x;
  }
  if (tx + tw > cell.x + cell.width) {
    tx = std::max(cell.x, cell.x + cell.width - tw);
  }
  renderer.drawText(fontId, tx, ty, fitted.c_str(), !whiteText, style);
}

void drawDayCell(const GfxRenderer& renderer, const Rect& cell, const int day, const bool selected,
                 const bool isToday) {
  char dayBuf[4];
  snprintf(dayBuf, sizeof(dayBuf), "%d", day);

  const int markSize = std::min(cell.width, cell.height) - 6;
  const int cx = cell.x + cell.width / 2;
  const int cy = cell.y + cell.height / 2;
  const int mx = cx - markSize / 2;
  const int my = cy - markSize / 2;

  if (selected) {
    renderer.fillRoundedRect(mx, my, markSize, markSize, markSize / 2, Color::Black);
    drawCenteredInCell(renderer, cell, NOTOSANS_14_FONT_ID, dayBuf, true, EpdFontFamily::BOLD);
    return;
  }

  if (isToday) {
    renderer.drawRoundedRect(mx, my, markSize, markSize, 1, markSize / 2, true);
    renderer.fillRectDither(mx + 2, my + 2, markSize - 4, markSize - 4, Color::LightGray);
  }

  drawCenteredInCell(renderer, cell, NOTOSANS_14_FONT_ID, dayBuf, false, EpdFontFamily::REGULAR);
}

void drawMonthTitleBand(const GfxRenderer& renderer, const Rect& band, const int year, const int month) {
  drawPanel(renderer, band, Color::White);

  char yearBuf[8];
  snprintf(yearBuf, sizeof(yearBuf), "%d", year);

  const char* monthText = monthName(month);
  const int monthW = renderer.getTextWidth(NOTOSANS_18_FONT_ID, monthText, EpdFontFamily::BOLD);
  const int yearW = renderer.getTextWidth(UI_10_FONT_ID, yearBuf, EpdFontFamily::REGULAR);
  const int monthH = renderer.getLineHeight(NOTOSANS_18_FONT_ID);
  const int monthX = band.x + (band.width - monthW) / 2;
  const int monthY = band.y + (band.height - monthH) / 2 - 2;
  renderer.drawText(NOTOSANS_18_FONT_ID, monthX, monthY, monthText, true, EpdFontFamily::BOLD);

  const int yearX = band.x + band.width - UITheme::getInstance().getMetrics().contentSidePadding - yearW;
  const int yearY = band.y + (band.height - renderer.getLineHeight(UI_10_FONT_ID)) / 2;
  renderer.drawText(UI_10_FONT_ID, yearX, yearY, yearBuf, true, EpdFontFamily::REGULAR);
}

void drawDateDetailCard(const GfxRenderer& renderer, const Rect& card, const int year, const int month,
                        const int day) {
  drawPanel(renderer, card, Color::White);

  LunarDate lunar{};
  LunarCalendar::gregorianToLunar(year, month, day, lunar);

  const int wday = LunarCalendar::weekdaySun0(year, month, day);
  char gregLine[24];
  char lunarLine[24];
  formatGregorianMmmDdYyyy(year, month, day, gregLine, sizeof(gregLine));
  formatLunarMmmDdYyyy(lunar, lunarLine, sizeof(lunarLine));

  const auto& pad = UITheme::getInstance().getMetrics().contentSidePadding;
  const int midX = card.x + card.width / 2;

  constexpr int kLabelFont = UI_10_FONT_ID;
  constexpr int kDateFont = UI_10_FONT_ID;
  constexpr int kWeekdayFont = NOTOSANS_12_FONT_ID;

  const int labelLineH = renderer.getLineHeight(kLabelFont);
  const int dateLineH = renderer.getLineHeight(kDateFont);
  const int weekdayLineH = renderer.getLineHeight(kWeekdayFont);

  constexpr int kWeekdayStripH = 28;
  const int weekdayBarTop = card.y + card.height - kWeekdayStripH;
  const int datesAreaBottom = weekdayBarTop - 6;

  const int labelY = card.y + 8;
  const int valueY = labelY + labelLineH + 6;
  const int weekdayY = weekdayBarTop + (kWeekdayStripH - weekdayLineH) / 2;

  renderer.drawLine(midX, card.y + 6, midX, datesAreaBottom, 1, true);
  renderer.drawLine(card.x + pad, weekdayBarTop, card.x + card.width - pad, weekdayBarTop, 1, true);

  renderer.drawText(kLabelFont, card.x + pad, labelY, tr(STR_CALENDAR_GREGORIAN), true, EpdFontFamily::REGULAR);
  renderer.drawText(kLabelFont, midX + pad, labelY, tr(STR_CALENDAR_LUNAR), true, EpdFontFamily::REGULAR);

  renderer.drawText(kDateFont, card.x + pad, valueY, gregLine, true, EpdFontFamily::REGULAR);
  renderer.drawText(kDateFont, midX + pad, valueY, lunarLine, true, EpdFontFamily::REGULAR);

  const char* wdayText = weekdayName(wday);
  const int wdayW = renderer.getTextWidth(kWeekdayFont, wdayText, EpdFontFamily::REGULAR);
  const int wdayX = card.x + (card.width - wdayW) / 2;
  renderer.drawText(kWeekdayFont, wdayX, weekdayY, wdayText, true, EpdFontFamily::REGULAR);
}

int todayStripHeight(const GfxRenderer& renderer) {
  const int lineH10 = renderer.getLineHeight(UI_10_FONT_ID);
  constexpr int kPad = 5;
  constexpr int kGap = 2;
  return kPad + lineH10 + kGap + lineH10 + kGap + lineH10 + kPad;
}

void drawTodayStrip(const GfxRenderer& renderer, const Rect& strip, const ConvertViewState& state) {
  const auto& pad = UITheme::getInstance().getMetrics().contentSidePadding;
  constexpr int kInnerPad = 5;
  constexpr int kGap = 2;
  drawPanel(renderer, strip, Color::LightGray);

  const int lineH10 = renderer.getLineHeight(UI_10_FONT_ID);
  int y = strip.y + kInnerPad;
  renderer.drawText(UI_10_FONT_ID, strip.x + pad, y, tr(STR_CALENDAR_TODAY), true, EpdFontFamily::REGULAR);
  y += lineH10 + kGap;

  char gregBuf[24];
  char lunarBuf[24];
  formatGregorianMmmDdYyyy(state.todayGregYear, state.todayGregMonth, state.todayGregDay, gregBuf, sizeof(gregBuf));
  LunarDate todayLunar{};
  todayLunar.year = state.todayLunarYear;
  todayLunar.month = state.todayLunarMonth;
  todayLunar.day = state.todayLunarDay;
  todayLunar.leapMonth = state.todayLunarLeap;
  formatLunarMmmDdYyyy(todayLunar, lunarBuf, sizeof(lunarBuf));

  const int midX = strip.x + strip.width / 2;
  const int colPad = pad;
  const int colW = strip.width / 2 - colPad;
  const int lineBottom = strip.y + strip.height - kInnerPad;
  renderer.drawLine(midX, y, midX, lineBottom, 1, true);

  const int leftX = strip.x + colPad;
  const int rightX = midX + colPad;
  renderer.drawText(UI_10_FONT_ID, leftX, y, tr(STR_CALENDAR_GREGORIAN), true, EpdFontFamily::REGULAR);
  renderer.drawText(UI_10_FONT_ID, rightX, y, tr(STR_CALENDAR_LUNAR), true, EpdFontFamily::REGULAR);
  y += lineH10 + kGap;

  const std::string gregDate = renderer.truncatedText(UI_10_FONT_ID, gregBuf, colW - 4);
  const std::string lunarDate = renderer.truncatedText(UI_10_FONT_ID, lunarBuf, colW - 4);
  renderer.drawText(UI_10_FONT_ID, leftX, y, gregDate.c_str(), true, EpdFontFamily::REGULAR);
  renderer.drawText(UI_10_FONT_ID, rightX, y, lunarDate.c_str(), true, EpdFontFamily::REGULAR);
}

void drawSourceToggle(const GfxRenderer& renderer, const Rect& row, const bool gregorianSelected,
                      const bool highlighted) {
  const auto& pad = UITheme::getInstance().getMetrics().contentSidePadding;
  const int lineH = renderer.getLineHeight(UI_10_FONT_ID);
  if (highlighted) {
    renderer.fillRoundedRect(row.x + pad, row.y + 1, row.width - pad * 2, row.height - 2, 4, Color::LightGray);
  }
  renderer.drawText(UI_10_FONT_ID, row.x + pad, row.y + 5, tr(STR_CALENDAR_I_KNOW), true, EpdFontFamily::REGULAR);

  constexpr int kPillH = 26;
  const int toggleY = row.y + 5 + lineH + 5;
  const int toggleH = kPillH;
  const int toggleW = (row.width - pad * 2 - 6) / 2;
  const int toggleX = row.x + pad;

  const auto drawPill = [&](const int x, const char* label, const bool selected) {
    const int w = toggleW - 3;
    if (selected) {
      renderer.fillRoundedRect(x, toggleY, w, toggleH, 6, Color::Black);
      renderer.drawText(UI_10_FONT_ID, x + 6, toggleY + (toggleH - lineH) / 2, label, false, EpdFontFamily::BOLD);
    } else {
      renderer.drawRoundedRect(x, toggleY, w, toggleH, 1, 6, true);
      renderer.drawText(UI_10_FONT_ID, x + 6, toggleY + (toggleH - lineH) / 2, label, true, EpdFontFamily::REGULAR);
    }
  };

  drawPill(toggleX, tr(STR_CALENDAR_GREGORIAN), gregorianSelected);
  drawPill(toggleX + toggleW, tr(STR_CALENDAR_LUNAR), !gregorianSelected);
}

void drawSourceFieldRow(const GfxRenderer& renderer, const Rect& row, const char* label, const char* value,
                        const bool highlighted, const bool editingHighlight) {
  const auto& pad = UITheme::getInstance().getMetrics().contentSidePadding;
  const int lineH10 = renderer.getLineHeight(UI_10_FONT_ID);
  const int labelX = row.x + pad + 6;
  const int labelY = row.y + (row.height - lineH10) / 2;
  constexpr int kValueFont = UI_10_FONT_ID;
  const int lineHVal = renderer.getLineHeight(kValueFont);
  const int valueW = renderer.getTextWidth(kValueFont, value, EpdFontFamily::BOLD);
  const int valueX = row.x + row.width - pad - 8 - valueW;
  const int valueY = row.y + (row.height - lineHVal) / 2;

  if (highlighted) {
    renderer.fillRoundedRect(row.x + pad, row.y + 1, row.width - pad * 2, row.height - 2, 4, Color::LightGray);
  }

  renderer.drawText(UI_10_FONT_ID, labelX, labelY, label, true, EpdFontFamily::REGULAR);
  if (highlighted && editingHighlight) {
    renderer.fillRoundedRect(valueX - 4, row.y + 3, valueW + 8, row.height - 6, 4, Color::Black);
    renderer.drawText(kValueFont, valueX, valueY, value, false, EpdFontFamily::BOLD);
  } else {
    renderer.drawText(kValueFont, valueX, valueY, value, true, EpdFontFamily::BOLD);
  }
}

constexpr int kPanelInnerPad = 4;
constexpr int kPanelLineGap = 2;

int panelHeaderHeight(const GfxRenderer& renderer) {
  const int lineH10 = renderer.getLineHeight(UI_10_FONT_ID);
  const int lineH14 = renderer.getLineHeight(NOTOSANS_14_FONT_ID);
  return kPanelInnerPad + lineH10 + kPanelLineGap + lineH14 + kPanelInnerPad;
}

int panelFieldRowHeight(const GfxRenderer& renderer) {
  return std::max(22, renderer.getLineHeight(UI_10_FONT_ID) + 6);
}

int sourcePanelContentHeight(const GfxRenderer& renderer) {
  return panelHeaderHeight(renderer) + 3 * panelFieldRowHeight(renderer);
}

int resultPanelContentHeight(const GfxRenderer& renderer, const bool sourceIsGregorian) {
  const int rows = sourceIsGregorian ? 2 : 1;
  return panelHeaderHeight(renderer) + rows * panelFieldRowHeight(renderer);
}

void drawSourcePanel(const GfxRenderer& renderer, const Rect& panel, const ConvertViewState& state) {
  const auto& pad = UITheme::getInstance().getMetrics().contentSidePadding;
  const int lineH10 = renderer.getLineHeight(UI_10_FONT_ID);
  const int lineH14 = renderer.getLineHeight(NOTOSANS_14_FONT_ID);
  const int fieldRowH = panelFieldRowHeight(renderer);

  drawPanel(renderer, panel, Color::White);
  renderer.drawText(UI_10_FONT_ID, panel.x + pad, panel.y + kPanelInnerPad, tr(STR_CALENDAR_SOURCE), true,
                    EpdFontFamily::REGULAR);

  char bigDate[28];
  if (state.sourceIsGregorian) {
    formatGregorianMmmDdYyyy(state.gregYear, state.gregMonth, state.gregDay, bigDate, sizeof(bigDate));
  } else {
    LunarDate lunar{};
    lunar.year = state.lunarYear;
    lunar.month = state.lunarMonth;
    lunar.day = state.lunarDay;
    lunar.leapMonth = state.lunarLeap;
    formatLunarMmmDdYyyy(lunar, bigDate, sizeof(bigDate));
  }
  const int bigDateY = panel.y + kPanelInnerPad + lineH10 + kPanelLineGap;
  renderer.drawText(NOTOSANS_14_FONT_ID, panel.x + pad, bigDateY,
                    renderer.truncatedText(NOTOSANS_14_FONT_ID, bigDate, panel.width - pad * 2).c_str(), true,
                    EpdFontFamily::BOLD);

  char yearBuf[16];
  char monthBuf[8];
  char dayBuf[8];
  if (state.sourceIsGregorian) {
    snprintf(yearBuf, sizeof(yearBuf), "%d", state.gregYear);
    snprintf(monthBuf, sizeof(monthBuf), "%02d", state.gregMonth);
    snprintf(dayBuf, sizeof(dayBuf), "%02d", state.gregDay);
  } else {
    snprintf(yearBuf, sizeof(yearBuf), "%d", state.lunarYear);
    if (state.lunarLeap) {
      snprintf(monthBuf, sizeof(monthBuf), "%02d L", state.lunarMonth);
    } else {
      snprintf(monthBuf, sizeof(monthBuf), "%02d", state.lunarMonth);
    }
    snprintf(dayBuf, sizeof(dayBuf), "%02d", state.lunarDay);
  }

  const int rowTop = bigDateY + lineH14 + kPanelLineGap;
  const int sourceRow = state.selectedField >= 1 ? state.selectedField - 1 : -1;
  for (int i = 0; i < 3; ++i) {
    const Rect row{panel.x, rowTop + i * fieldRowH, panel.width, fieldRowH};
    const char* val = (i == 0) ? yearBuf : (i == 1) ? monthBuf : dayBuf;
    drawSourceFieldRow(renderer, row, I18N.get(kFieldLabelIds[i]), val, sourceRow == i,
                       state.editing && sourceRow == i);
  }
}

void drawResultPanel(const GfxRenderer& renderer, const Rect& panel, const ConvertViewState& state) {
  const auto& pad = UITheme::getInstance().getMetrics().contentSidePadding;
  const int lineH10 = renderer.getLineHeight(UI_10_FONT_ID);
  const int lineH14 = renderer.getLineHeight(NOTOSANS_14_FONT_ID);
  const int fieldRowH = panelFieldRowHeight(renderer);

  drawPanel(renderer, panel, Color::White);
  renderer.drawText(UI_10_FONT_ID, panel.x + pad, panel.y + kPanelInnerPad, tr(STR_CALENDAR_RESULT), true,
                    EpdFontFamily::REGULAR);

  char bigDate[28];
  int resultGregYear = state.gregYear;
  int resultGregMonth = state.gregMonth;
  int resultGregDay = state.gregDay;
  if (state.sourceIsGregorian) {
    LunarDate lunar{};
    LunarCalendar::gregorianToLunar(state.gregYear, state.gregMonth, state.gregDay, lunar);
    formatLunarMmmDdYyyy(lunar, bigDate, sizeof(bigDate));
  } else {
    int lunarDay = state.lunarDay;
    bool lunarLeap = state.lunarLeap;
    if (!LunarCalendar::resolveLunarToGregorian(state.lunarYear, state.lunarMonth, lunarDay, lunarLeap,
                                                resultGregYear, resultGregMonth, resultGregDay)) {
      resultGregYear = state.gregYear;
      resultGregMonth = state.gregMonth;
      resultGregDay = state.gregDay;
    }
    formatGregorianMmmDdYyyy(resultGregYear, resultGregMonth, resultGregDay, bigDate, sizeof(bigDate));
  }
  const int bigDateY = panel.y + kPanelInnerPad + lineH10 + kPanelLineGap;
  renderer.drawText(NOTOSANS_14_FONT_ID, panel.x + pad, bigDateY,
                    renderer.truncatedText(NOTOSANS_14_FONT_ID, bigDate, panel.width - pad * 2).c_str(), true,
                    EpdFontFamily::BOLD);

  const int wday = LunarCalendar::weekdaySun0(resultGregYear, resultGregMonth, resultGregDay);
  const int rowTop = bigDateY + lineH14 + kPanelLineGap;
  int rowIndex = 0;

  if (state.sourceIsGregorian) {
    LunarDate resultLunar{};
    LunarCalendar::gregorianToLunar(state.gregYear, state.gregMonth, state.gregDay, resultLunar);
    char leapVal[8];
    snprintf(leapVal, sizeof(leapVal), "%s", resultLunar.leapMonth ? tr(STR_YES) : tr(STR_NO));
    drawSourceFieldRow(renderer, Rect{panel.x, rowTop + rowIndex * fieldRowH, panel.width, fieldRowH},
                       tr(STR_CALENDAR_LEAP_MONTH), leapVal, false, false);
    rowIndex++;
  }

  drawSourceFieldRow(renderer, Rect{panel.x, rowTop + rowIndex * fieldRowH, panel.width, fieldRowH}, tr(STR_WEEKDAY),
                     I18N.get(weekdayStrId(wday)), false, false);
}

void drawConvertsToLabel(const GfxRenderer& renderer, const Rect& area) {
  const auto& pad = UITheme::getInstance().getMetrics().contentSidePadding;
  const int lineH = renderer.getLineHeight(UI_10_FONT_ID);
  const char* label = tr(STR_CALENDAR_CONVERTS_TO);
  const int textW = renderer.getTextWidth(UI_10_FONT_ID, label, EpdFontFamily::REGULAR);
  const int textX = area.x + (area.width - textW) / 2;
  const int textY = area.y + (area.height - lineH) / 2;
  const int lineY = textY + lineH / 2;
  constexpr int kLineGap = 8;
  const int leftEnd = textX - kLineGap;
  const int rightStart = textX + textW + kLineGap;
  if (leftEnd > area.x + pad) {
    renderer.drawLine(area.x + pad, lineY, leftEnd, lineY, 1, true);
  }
  if (rightStart < area.x + area.width - pad) {
    renderer.drawLine(rightStart, lineY, area.x + area.width - pad, lineY, 1, true);
  }
  renderer.drawText(UI_10_FONT_ID, textX, textY, label, true, EpdFontFamily::REGULAR);
}

struct ConvertLayoutRects {
  Rect today;
  Rect toggle;
  Rect arrow;
  Rect source;
  Rect result;
};

ConvertLayoutRects computeConvertLayout(const GfxRenderer& renderer, const ConvertViewState& state,
                                        const int contentTop, const int contentBottom, const int blockX,
                                        const int blockW, const int gap) {
  const int lineH10 = renderer.getLineHeight(UI_10_FONT_ID);
  constexpr int kTogglePillH = 26;
  constexpr int kConvertGap = 8;
  const int todayH = state.hasToday ? todayStripHeight(renderer) : 0;
  const int toggleH = 5 + lineH10 + 5 + kTogglePillH + 5;
  const int arrowH = lineH10 + 10;
  const int sourceH = sourcePanelContentHeight(renderer);
  const int resultH = resultPanelContentHeight(renderer, state.sourceIsGregorian);

  int blockCount = 3;
  if (state.hasToday) {
    blockCount++;
  }
  const int gapCount = blockCount + 1;
  const int stackH = todayH + toggleH + arrowH + sourceH + resultH + gapCount * kConvertGap;
  int y = contentTop;
  if (stackH < contentBottom - contentTop) {
    y += (contentBottom - contentTop - stackH) / 2;
  }

  ConvertLayoutRects layout{};

  if (state.hasToday) {
    layout.today = Rect{blockX, y, blockW, todayH};
    y += todayH + kConvertGap;
  }

  layout.toggle = Rect{blockX, y, blockW, toggleH};
  y += toggleH + kConvertGap;

  layout.source = Rect{blockX, y, blockW, sourceH};
  y += sourceH + kConvertGap;

  layout.arrow = Rect{blockX, y, blockW, arrowH};
  y += arrowH + kConvertGap;

  layout.result = Rect{blockX, y, blockW, resultH};
  return layout;
}

}  // namespace

void drawMonthView(const GfxRenderer& renderer, const MonthViewState& state) {
  const auto& m = UITheme::getInstance().getMetrics();
  const int pageW = renderer.getScreenWidth();
  const int pageH = renderer.getScreenHeight();
  const int pad = m.contentSidePadding;

  const int headerBottom = m.topPadding + m.headerHeight;
  GUI.drawHeader(renderer, Rect{0, m.topPadding, pageW, m.headerHeight}, tr(STR_CALENDAR_APP));

  const int contentTop = headerBottom + m.verticalSpacing;
  const int hintsTop = pageH - m.buttonHintsHeight - m.verticalSpacing;
  constexpr int kDetailH = 96;
  const int titleH = 46;
  const int detailTop = hintsTop - kDetailH - m.verticalSpacing;
  const int gridTop = contentTop + titleH + m.verticalSpacing;
  const int gridBottom = detailTop - m.verticalSpacing;
  const int gridH = std::max(0, gridBottom - gridTop);

  const Rect titleBand{pad, contentTop, pageW - pad * 2, titleH};
  drawMonthTitleBand(renderer, titleBand, state.viewYear, state.viewMonth);

  const Rect gridCard{pad, gridTop, pageW - pad * 2, gridH};
  drawPanel(renderer, gridCard, Color::White);

  const int innerPad = 10;
  const int weekdayH = 24;
  const Rect weekdayRow{gridCard.x + innerPad, gridCard.y + innerPad, gridCard.width - innerPad * 2, weekdayH};
  renderer.fillRectDither(weekdayRow.x, weekdayRow.y, weekdayRow.width, weekdayRow.height, Color::LightGray);

  const int cellW = weekdayRow.width / kWeekdayLabelCount;
  for (int i = 0; i < kWeekdayLabelCount; ++i) {
    const Rect labelCell{weekdayRow.x + i * cellW, weekdayRow.y, cellW, weekdayRow.height};
    drawCenteredInCell(renderer, labelCell, UI_10_FONT_ID, kWeekdayLabels[i], false, EpdFontFamily::REGULAR);
  }

  const int daysTop = weekdayRow.y + weekdayRow.height + 4;
  const int daysBottom = gridCard.y + gridCard.height - innerPad;
  const int daysH = std::max(0, daysBottom - daysTop);
  const int maxRows = 6;
  const int cellH = daysH > 0 ? std::max(26, daysH / maxRows) : 26;

  const int dim = LunarCalendar::daysInGregorianMonth(state.viewYear, state.viewMonth);
  const int firstWday = LunarCalendar::weekdaySun0(state.viewYear, state.viewMonth, 1);

  for (int d = 1; d <= dim; ++d) {
    const int idx = firstWday + d - 1;
    const int row = idx / 7;
    const int col = idx % 7;
    const int x = weekdayRow.x + col * cellW;
    const int y = daysTop + row * cellH;
    if (y + cellH > daysBottom) {
      break;
    }
    const Rect cell{x, y, cellW, cellH};
    const bool selected = (d == state.selectedDay);
    const bool isToday = state.hasToday && state.todayYear == state.viewYear && state.todayMonth == state.viewMonth &&
                         state.todayDay == d;
    drawDayCell(renderer, cell, d, selected, isToday);
  }

  if (state.selectedDay > 0) {
    const Rect detailCard{pad, detailTop, pageW - pad * 2, kDetailH};
    drawDateDetailCard(renderer, detailCard, state.viewYear, state.viewMonth, state.selectedDay);
  }
}

void drawConvertView(const GfxRenderer& renderer, const ConvertViewState& state) {
  const auto& m = UITheme::getInstance().getMetrics();
  const int pageW = renderer.getScreenWidth();
  const int pageH = renderer.getScreenHeight();
  const int pad = m.contentSidePadding;
  const int gap = m.verticalSpacing;

  const int headerBottom = m.topPadding + m.headerHeight;
  GUI.drawHeader(renderer, Rect{0, m.topPadding, pageW, m.headerHeight}, tr(STR_CALENDAR_CONVERT));

  const int contentTop = headerBottom + gap;
  const int contentBottom = pageH - m.buttonHintsHeight - gap;
  const int blockW = pageW - pad * 2;
  const ConvertLayoutRects layout =
      computeConvertLayout(renderer, state, contentTop, contentBottom, pad, blockW, gap);

  if (state.hasToday && layout.today.height > 0) {
    drawTodayStrip(renderer, layout.today, state);
  }

  drawSourceToggle(renderer, layout.toggle, state.sourceIsGregorian, state.selectedField == 0);

  if (layout.source.height > 0) {
    drawSourcePanel(renderer, layout.source, state);
  }

  drawConvertsToLabel(renderer, layout.arrow);

  if (layout.result.height > 0) {
    drawResultPanel(renderer, layout.result, state);
  }
}

}  // namespace CalendarLayout

#endif
