#include "MediaTileDraw.h"

#include <algorithm>
#include <cstdio>

#include "fontIds.h"

namespace MediaTileDraw {

void formatMmSs(const uint32_t sec, char* buf, const size_t bufLen) {
  const uint32_t m = sec / 60;
  const uint32_t s = sec % 60;
  snprintf(buf, bufLen, "%02u:%02u", m, s);
}

void drawThinProgressBar(const GfxRenderer& renderer, const Rect& rect, const uint32_t elapsedSec,
                         const uint32_t durationSec) {
  constexpr int kBorder = 2;
  constexpr int kInset = 3;
  renderer.drawRect(rect.x, rect.y, rect.width, rect.height, kBorder, true);
  if (durationSec == 0) {
    return;
  }
  const uint32_t clamped = std::min(elapsedSec, durationSec);
  const int innerW = std::max(0, rect.width - 2 * kInset);
  const int innerH = std::max(0, rect.height - 2 * kInset);
  int fillW = innerW > 0 ? static_cast<int>((static_cast<uint64_t>(clamped) * innerW) / durationSec) : 0;
  fillW = std::min(fillW, innerW);
  if (fillW > 0 && innerH > 0) {
    renderer.fillRect(rect.x + kInset, rect.y + kInset, fillW, innerH, true);
  }
}

void drawHorizontalVolume(const GfxRenderer& renderer, const int barX, const int barY, const int barW, const int barH,
                          const uint8_t volumePct) {
  renderer.drawRect(barX, barY, barW, barH, true);
  const int fillW = std::max(0, (barW - 4) * static_cast<int>(volumePct) / 100);
  if (fillW > 0) {
    renderer.fillRect(barX + 2, barY + 2, fillW, barH - 4, true);
  }
}

void drawHmsClock(const GfxRenderer& renderer, const Rect& inner, const uint32_t elapsedSec) {
  const uint32_t hours = elapsedSec / 3600;
  const uint32_t minutes = (elapsedSec % 3600) / 60;
  const uint32_t seconds = elapsedSec % 60;

  char main[12];
  char sec[4];
  snprintf(main, sizeof(main), "%02u:%02u", hours, minutes);
  snprintf(sec, sizeof(sec), "%02u", seconds);

  constexpr int kMainFont = NOTOSANS_18_FONT_ID;
  constexpr int kSecFont = NOTOSANS_12_FONT_ID;
  constexpr EpdFontFamily::Style kStyle = EpdFontFamily::BOLD;
  constexpr int kSecGap = 6;

  const int mainW = renderer.getTextWidth(kMainFont, main, kStyle);
  const int secW = renderer.getTextWidth(kSecFont, sec, kStyle);
  const int mainLineH = renderer.getLineHeight(kMainFont);
  const int secLineH = renderer.getLineHeight(kSecFont);
  const int mainY = inner.y + inner.height / 2 - mainLineH / 2;
  const int secY = inner.y + inner.height / 2 - secLineH / 2;

  const int totalW = mainW + kSecGap + secW;
  const int startX = inner.x + (inner.width - totalW) / 2;
  renderer.drawText(kMainFont, startX, mainY, main, true, kStyle);
  renderer.drawText(kSecFont, startX + mainW + kSecGap, secY, sec, true, kStyle);
}

void drawVerticalVolume(const GfxRenderer& renderer, const int x, const int y, const int w, const int h,
                        const uint8_t volumePct) {
  renderer.drawRect(x, y, w, h, true);
  const int fillH = std::max(0, (h - 4) * static_cast<int>(volumePct) / 100);
  if (fillH > 0) {
    renderer.fillRect(x + 2, y + h - 2 - fillH, w - 4, fillH, true);
  }
}

void drawPlaybackFooter(const GfxRenderer& renderer, const int innerX, const int innerW, const int tileY,
                        const int tileH, const uint32_t elapsedSec, const uint32_t durationSec) {
  char elapsedStr[8];
  char totalStr[8];
  formatMmSs(elapsedSec, elapsedStr, sizeof(elapsedStr));
  if (durationSec > 0) {
    formatMmSs(durationSec, totalStr, sizeof(totalStr));
  } else {
    snprintf(totalStr, sizeof(totalStr), "--:--");
  }

  const int timeY = playbackTimeY(tileY, tileH);
  renderer.drawText(UI_10_FONT_ID, innerX, timeY, elapsedStr, true);
  const int totalW = renderer.getTextWidth(UI_10_FONT_ID, totalStr);
  renderer.drawText(UI_10_FONT_ID, innerX + innerW - totalW, timeY, totalStr, true);

  const Rect barRect{innerX, playbackBarY(tileY, tileH), innerW, kPlaybackProgressBarH};
  drawThinProgressBar(renderer, barRect, elapsedSec, durationSec > 0 ? durationSec : elapsedSec + 1);
}

void erasePlaybackFooterBand(const GfxRenderer& renderer, const int innerX, const int innerW, const int tileY,
                             const int tileH) {
  const int bandTop = playbackTimeY(tileY, tileH) - 2;
  const int bandH = tileY + tileH - kPlaybackBottomPad - bandTop;
  if (bandH > 0) {
    renderer.fillRect(innerX, bandTop, innerW, bandH, false);
  }
}

}  // namespace MediaTileDraw
