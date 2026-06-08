#pragma once

#include <GfxRenderer.h>
#include <cstdint>
#include <string>

#include "components/themes/BaseTheme.h"

namespace UnifiedAppLayout {

constexpr int kMenuVisibleRows = 7;
constexpr int kAlarmVisibleRows = 7;

struct SplitLayout {
  int contentTop = 0;
  Rect bigTile{};
  Rect menu{};
};

int bigTileHeight();
// Compact clock tile for info-heavy screens (device information, etc.).
constexpr int kCompactBigTileHeight = 104;
SplitLayout splitBelowHeader(const GfxRenderer& renderer, int headerBottomY, int tileHeight = -1);

bool pollRtcSecondTick(uint8_t& lastSecond);
bool pollElapsedSecondTick(unsigned long& lastTickMs);

void drawTileSurface(const GfxRenderer& renderer, const Rect& tile, bool selected = false);
void drawSpreadText(const GfxRenderer& renderer, const Rect& tile, int fontId, const char* text,
                    int targetWidthPercent, int yPercentFromTop,
                    EpdFontFamily::Style style = EpdFontFamily::BOLD);
bool drawImageCoverInRoundedTile(GfxRenderer& renderer, const std::string& path, const Rect& tile);
// HH : MM (large) + SS (small) — shared by clock, alarms, device health, stopwatch, countdown.
void drawBigTimeTile(const GfxRenderer& renderer, const Rect& tile, uint8_t hour, uint8_t minute,
                     uint8_t second, bool valid = true);
void drawBigRtcClockTile(const GfxRenderer& renderer, const Rect& tile);
void drawBigElapsedMsTile(const GfxRenderer& renderer, const Rect& tile, unsigned long elapsedMs);
// Redraw elapsed/stopwatch digits inside an existing tile (no full-screen refresh).
void patchBigElapsedMsTile(const GfxRenderer& renderer, const Rect& tile, unsigned long elapsedMs);
// Small date caption along the bottom inside a clock tile.
void drawRtcDateCaptionInTile(const GfxRenderer& renderer, const Rect& tile);
// Rounded panel in the menu band with up to three centered hint lines.
void drawMenuHintPanel(const GfxRenderer& renderer, const Rect& menu, const char* line1, const char* line2,
                       const char* line3 = nullptr);

}  // namespace UnifiedAppLayout
