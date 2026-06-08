#include "UnifiedAppLayout.h"

#if defined(BOARD_ESP32_S3_EPAPER_397)

#include <Bitmap.h>
#include <FsHelpers.h>
#include <HalBoard397.h>
#include <GfxRenderer.h>
#include <HalStorage.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "Epub/converters/ImageToFramebufferDecoder.h"
#include "Epub/converters/JpegToFramebufferConverter.h"
#include "Epub/converters/PngToFramebufferConverter.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace UnifiedAppLayout {
namespace {

constexpr int kTileCornerRadius = 6;

Rect paddedTileRect(const Rect& tile, const int pad) {
  return Rect{tile.x + pad, tile.y + pad, tile.width - pad * 2, tile.height - pad * 2};
}

void drawSpreadTextImpl(const GfxRenderer& renderer, const Rect& tile, const int fontId, const char* text,
                        const int targetWidthPercent, const int yPercentFromTop, const EpdFontFamily::Style style);

void computeCoverDraw(const int imgW, const int imgH, const Rect& box, int& drawW, int& drawH, int& drawX, int& drawY,
                      float& cropX, float& cropY) {
  drawW = box.width;
  drawH = box.height;
  drawX = box.x;
  drawY = box.y;
  cropX = 0.0f;
  cropY = 0.0f;
  if (imgW <= 0 || imgH <= 0) {
    return;
  }
  const float imgAspect = static_cast<float>(imgW) / static_cast<float>(imgH);
  const float boxAspect = static_cast<float>(box.width) / static_cast<float>(box.height);
  if (imgAspect > boxAspect) {
    const float visibleW = static_cast<float>(imgH) * boxAspect;
    cropX = 1.0f - visibleW / static_cast<float>(imgW);
  } else if (imgAspect < boxAspect) {
    const float visibleH = static_cast<float>(imgW) / boxAspect;
    cropY = 1.0f - visibleH / static_cast<float>(imgH);
  }
}

// Centered cover-fit placement (may extend outside box; clip when rendering).
void computeCoverPlacement(const int imgW, const int imgH, const Rect& box, int& drawW, int& drawH, int& drawX,
                           int& drawY) {
  drawX = box.x;
  drawY = box.y;
  drawW = box.width;
  drawH = box.height;
  if (imgW <= 0 || imgH <= 0 || box.width <= 0 || box.height <= 0) {
    return;
  }
  const float scale =
      std::max(static_cast<float>(box.width) / imgW, static_cast<float>(box.height) / imgH);
  drawW = static_cast<int>(imgW * scale + 0.5f);
  drawH = static_cast<int>(imgH * scale + 0.5f);
  drawX = box.x + (box.width - drawW) / 2;
  drawY = box.y + (box.height - drawH) / 2;
}

int spreadTextNaturalWidth(const GfxRenderer& renderer, const int fontId, const char* text,
                           const EpdFontFamily::Style style) {
  int naturalW = 0;
  for (const char* p = text; *p != '\0'; ++p) {
    char ch[2] = {*p, '\0'};
    naturalW += renderer.getTextWidth(fontId, ch, style);
  }
  return naturalW;
}

int drawSpreadTextAt(const GfxRenderer& renderer, const int x, const int y, const int fontId, const char* text,
                     const int targetWidth, const EpdFontFamily::Style style) {
  if (text == nullptr || text[0] == '\0') {
    return 0;
  }
  const int naturalW = spreadTextNaturalWidth(renderer, fontId, text, style);
  float spacing = 1.0f;
  if (naturalW > 0 && targetWidth > naturalW) {
    spacing = static_cast<float>(targetWidth) / static_cast<float>(naturalW);
  }
  int cursor = x;
  for (const char* p = text; *p != '\0'; ++p) {
    char ch[2] = {*p, '\0'};
    renderer.drawText(fontId, cursor, y, ch, true, style);
    cursor += static_cast<int>(renderer.getTextWidth(fontId, ch, style) * spacing);
  }
  return static_cast<int>((naturalW > 0 ? naturalW : 0) * spacing);
}

void drawTextClockRow(const GfxRenderer& renderer, const Rect& inner, const uint8_t hour, const uint8_t minute,
                      const uint8_t second, const bool rtcValid) {
  char main[12];
  char sec[4];
  if (rtcValid) {
    snprintf(main, sizeof(main), "%02u:%02u", hour, minute);
    snprintf(sec, sizeof(sec), "%02u", second);
  } else {
    snprintf(main, sizeof(main), "--:--");
    snprintf(sec, sizeof(sec), "--");
  }

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

bool drawBmpCover(GfxRenderer& renderer, const std::string& path, const Rect& inner) {
  FsFile file;
  if (!Storage.openFileForRead("PIC", path, file)) {
    return false;
  }
  Bitmap bitmap(file);
  if (bitmap.parseHeaders() != BmpReaderError::Ok) {
    file.close();
    return false;
  }
  int drawW = 0;
  int drawH = 0;
  int drawX = 0;
  int drawY = 0;
  float cropX = 0.0f;
  float cropY = 0.0f;
  computeCoverDraw(bitmap.getWidth(), bitmap.getHeight(), inner, drawW, drawH, drawX, drawY, cropX, cropY);
  renderer.drawBitmap(bitmap, drawX, drawY, drawW, drawH, cropX, cropY);
  file.close();
  return true;
}

bool drawEncodedCover(GfxRenderer& renderer, const std::string& path, const Rect& inner) {
  ImageDimensions dims{};
  if (FsHelpers::hasPngExtension(path)) {
    if (!PngToFramebufferConverter::getDimensionsStatic(path, dims)) {
      return false;
    }
  } else if (FsHelpers::hasJpgExtension(path)) {
    if (!JpegToFramebufferConverter::getDimensionsStatic(path, dims)) {
      return false;
    }
  } else {
    return false;
  }

  int drawW = 0;
  int drawH = 0;
  int drawX = 0;
  int drawY = 0;
  computeCoverPlacement(dims.width, dims.height, inner, drawW, drawH, drawX, drawY);

  RenderConfig config{};
  config.x = drawX;
  config.y = drawY;
  config.maxWidth = drawW;
  config.maxHeight = drawH;
  renderConfigSetClipFromRect(config, inner.x, inner.y, inner.width, inner.height);
  config.useGrayscale = true;
  config.useDithering = true;
  config.useExactDimensions = true;

  if (FsHelpers::hasPngExtension(path)) {
    PngToFramebufferConverter png;
    return png.decodeToFramebuffer(path, renderer, config);
  }
  JpegToFramebufferConverter jpeg;
  return jpeg.decodeToFramebuffer(path, renderer, config);
}

void drawSpreadTextImpl(const GfxRenderer& renderer, const Rect& tile, const int fontId, const char* text,
                        const int targetWidthPercent, const int yPercentFromTop, const EpdFontFamily::Style style) {
  if (text == nullptr || text[0] == '\0') {
    return;
  }
  int naturalW = 0;
  for (const char* p = text; *p != '\0'; ++p) {
    char ch[2] = {*p, '\0'};
    naturalW += renderer.getTextWidth(fontId, ch, style);
  }
  const int targetW = tile.width * targetWidthPercent / 100;
  float spacing = 1.0f;
  if (naturalW > 0 && targetW > naturalW) {
    spacing = static_cast<float>(targetW) / static_cast<float>(naturalW);
  }
  int x = tile.x + (tile.width - static_cast<int>(naturalW * spacing)) / 2;
  const int lineH = renderer.getLineHeight(fontId);
  const int y = tile.y + tile.height * yPercentFromTop / 100 - lineH / 2;
  for (const char* p = text; *p != '\0'; ++p) {
    char ch[2] = {*p, '\0'};
    renderer.drawText(fontId, x, y, ch, true, style);
    x += static_cast<int>(renderer.getTextWidth(fontId, ch, style) * spacing);
  }
}

}  // namespace

void drawSpreadText(const GfxRenderer& renderer, const Rect& tile, const int fontId, const char* text,
                    const int targetWidthPercent, const int yPercentFromTop, const EpdFontFamily::Style style) {
  drawSpreadTextImpl(renderer, tile, fontId, text, targetWidthPercent, yPercentFromTop, style);
}

int bigTileHeight() { return UITheme::getInstance().getMetrics().homeCoverTileHeight; }

SplitLayout splitBelowHeader(const GfxRenderer& renderer, const int headerBottomY, const int tileHeight) {
  const auto& m = UITheme::getInstance().getMetrics();
  const int pageW = renderer.getScreenWidth();
  const int pageH = renderer.getScreenHeight();
  const int pad = m.contentSidePadding;
  const int contentTop = headerBottomY + m.verticalSpacing;
  const int tileH = tileHeight >= 0 ? tileHeight : bigTileHeight();
  const int menuTop = contentTop + tileH + m.verticalSpacing;
  const int menuBottom = pageH - m.buttonHintsHeight - m.verticalSpacing;
  SplitLayout layout;
  layout.contentTop = contentTop;
  layout.bigTile = Rect{pad, contentTop, pageW - pad * 2, tileH};
  layout.menu = Rect{0, menuTop, pageW, std::max(0, menuBottom - menuTop)};
  return layout;
}

bool pollRtcSecondTick(uint8_t& lastSecond) {
  HalBoard397::DateTime dt{};
  if (!board397.readRtcForDisplay(dt)) {
    return false;
  }
  if (dt.second != lastSecond) {
    lastSecond = dt.second;
    return true;
  }
  return false;
}

bool pollElapsedSecondTick(unsigned long& lastTickMs) {
  const unsigned long now = millis();
  if (now - lastTickMs >= 1000) {
    lastTickMs = now;
    return true;
  }
  return false;
}

void drawTileSurface(const GfxRenderer& renderer, const Rect& tile, const bool selected) {
  const Color fill = selected ? Color::LightGray : Color::White;
  renderer.fillRoundedRect(tile.x, tile.y, tile.width, tile.height, kTileCornerRadius, fill);
  renderer.drawRoundedRect(tile.x, tile.y, tile.width, tile.height, 1, kTileCornerRadius, true);
}

void drawBigTimeTile(const GfxRenderer& renderer, const Rect& tile, const uint8_t hour, const uint8_t minute,
                     const uint8_t second, const bool valid) {
  const Rect inner = paddedTileRect(tile, 8);
  drawTextClockRow(renderer, inner, hour, minute, second, valid);
}

void drawBigRtcClockTile(const GfxRenderer& renderer, const Rect& tile) {
  HalBoard397::DateTime dt{};
  const bool rtcValid = board397.readRtcForDisplay(dt);
  drawBigTimeTile(renderer, tile, dt.hour, dt.minute, dt.second, rtcValid);
}

void drawBigElapsedMsTile(const GfxRenderer& renderer, const Rect& tile, const unsigned long elapsedMs) {
  const unsigned long totalSec = elapsedMs / 1000;
  const uint8_t hour = static_cast<uint8_t>(std::min<unsigned long>(99, totalSec / 3600));
  const uint8_t minute = static_cast<uint8_t>((totalSec % 3600) / 60);
  const uint8_t second = static_cast<uint8_t>(totalSec % 60);
  drawBigTimeTile(renderer, tile, hour, minute, second, true);
}

void patchBigElapsedMsTile(const GfxRenderer& renderer, const Rect& tile, const unsigned long elapsedMs) {
  const Rect inner = paddedTileRect(tile, 8);
  renderer.fillRect(inner.x, inner.y, inner.width, inner.height, false);
  drawBigElapsedMsTile(renderer, tile, elapsedMs);
}

void drawRtcDateCaptionInTile(const GfxRenderer& renderer, const Rect& tile) {
  HalBoard397::DateTime dt{};
  if (!board397.readRtcForDisplay(dt)) {
    return;
  }
  static const char* kMonths[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                  "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
  char buf[40];
  if (dt.month >= 1 && dt.month <= 12) {
    snprintf(buf, sizeof(buf), "%s %u, %04u", kMonths[dt.month - 1], static_cast<unsigned>(dt.day), dt.year);
  } else {
    snprintf(buf, sizeof(buf), "%04u-%02u-%02u", dt.year, dt.month, dt.day);
  }
  const Rect inner = paddedTileRect(tile, 8);
  const int lineH = renderer.getLineHeight(UI_10_FONT_ID);
  const int textY = inner.y + inner.height - lineH - 2;
  const int textW = renderer.getTextWidth(UI_10_FONT_ID, buf);
  const int textX = inner.x + (inner.width - textW) / 2;
  renderer.drawText(UI_10_FONT_ID, textX, textY, buf, true);
}

void drawMenuHintPanel(const GfxRenderer& renderer, const Rect& menu, const char* line1, const char* line2,
                       const char* line3) {
  if (menu.width <= 0 || menu.height <= 0) {
    return;
  }
  drawTileSurface(renderer, menu);
  const auto& m = UITheme::getInstance().getMetrics();
  const int padX = m.contentSidePadding + 12;
  const int innerW = std::max(0, menu.width - padX * 2);
  const int lineH = renderer.getLineHeight(UI_10_FONT_ID);
  int lineCount = 0;
  if (line1 != nullptr && line1[0] != '\0') {
    ++lineCount;
  }
  if (line2 != nullptr && line2[0] != '\0') {
    ++lineCount;
  }
  if (line3 != nullptr && line3[0] != '\0') {
    ++lineCount;
  }
  if (lineCount == 0) {
    return;
  }
  const int blockH = lineCount * lineH + (lineCount - 1) * 6;
  int y = menu.y + (menu.height - blockH) / 2;
  auto drawLine = [&](const char* text) {
    if (text == nullptr || text[0] == '\0') {
      return;
    }
    const int tw = renderer.getTextWidth(UI_10_FONT_ID, text);
    const int tx = menu.x + (menu.width - tw) / 2;
    renderer.drawText(UI_10_FONT_ID, tx, y, text, true);
    y += lineH + 6;
  };
  drawLine(line1);
  drawLine(line2);
  drawLine(line3);
}

bool drawImageCoverInRoundedTile(GfxRenderer& renderer, const std::string& path, const Rect& tile) {
  if (path.empty()) {
    return false;
  }
  renderer.fillRoundedRect(tile.x, tile.y, tile.width, tile.height, kTileCornerRadius, Color::White);
  const Rect inner = paddedTileRect(tile, 2);
  bool ok = false;
  if (FsHelpers::hasBmpExtension(path)) {
    ok = drawBmpCover(renderer, path, inner);
  } else if (FsHelpers::hasPngExtension(path) || FsHelpers::hasJpgExtension(path)) {
    ok = drawEncodedCover(renderer, path, inner);
  }
  if (ok) {
    renderer.maskRoundedRectOutsideCorners(tile.x, tile.y, tile.width, tile.height, kTileCornerRadius, Color::White);
  }
  return ok;
}

}  // namespace UnifiedAppLayout

#endif
