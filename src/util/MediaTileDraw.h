#pragma once

#include <GfxRenderer.h>
#include <cstdint>

#include "components/themes/BaseTheme.h"

namespace MediaTileDraw {

// Playback footer: timeline labels + progress bar, lifted from tile bottom.
constexpr int kPlaybackProgressBarH = 12;
constexpr int kPlaybackTimeRowH = 14;
constexpr int kPlaybackTimeToBarGap = 10;
constexpr int kPlaybackBottomPad = 24;

inline int playbackTimeY(const int tileY, const int tileH) {
  return tileY + tileH - kPlaybackBottomPad - kPlaybackProgressBarH - kPlaybackTimeToBarGap - kPlaybackTimeRowH;
}
inline int playbackBarY(const int tileY, const int tileH) {
  return tileY + tileH - kPlaybackBottomPad - kPlaybackProgressBarH;
}
inline int playbackTitleBottom(const int tileY, const int tileH, const int gapAboveTime = 8) {
  return playbackTimeY(tileY, tileH) - gapAboveTime;
}

void formatMmSs(uint32_t sec, char* buf, size_t bufLen);
// Clock-style HH:MM + SS (18px + 12px), centered in rect — matches UnifiedAppLayout clock tile.
void drawHmsClock(const GfxRenderer& renderer, const Rect& inner, uint32_t elapsedSec);
void drawThinProgressBar(const GfxRenderer& renderer, const Rect& rect, uint32_t elapsedSec, uint32_t durationSec);
// Elapsed/total labels + progress bar at bottom of a media tile.
void drawPlaybackFooter(const GfxRenderer& renderer, int innerX, int innerW, int tileY, int tileH,
                        uint32_t elapsedSec, uint32_t durationSec);
// White-out the footer band before a partial redraw (avoids full-screen refresh).
void erasePlaybackFooterBand(const GfxRenderer& renderer, int innerX, int innerW, int tileY, int tileH);
void drawHorizontalVolume(const GfxRenderer& renderer, int barX, int barY, int barW, int barH, uint8_t volumePct);
void drawVerticalVolume(const GfxRenderer& renderer, int x, int y, int w, int h, uint8_t volumePct);

}  // namespace MediaTileDraw
