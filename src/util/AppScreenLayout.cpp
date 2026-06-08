#include "AppScreenLayout.h"

#include <algorithm>

#include "components/UITheme.h"
#include "fontIds.h"

namespace AppScreenLayout {

ContentInset rotatedReaderInsets(const GfxRenderer& renderer) {
  ContentInset inset;
  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();
  const auto orientation = renderer.getOrientation();
  const bool isLandscapeCw = orientation == GfxRenderer::Orientation::LandscapeClockwise;
  const bool isLandscapeCcw = orientation == GfxRenderer::Orientation::LandscapeCounterClockwise;
  const bool isPortraitInverted = orientation == GfxRenderer::Orientation::PortraitInverted;
  const int hintGutterWidth = (isLandscapeCw || isLandscapeCcw) ? 30 : 0;
  inset.x = isLandscapeCw ? hintGutterWidth : 0;
  inset.width = pageWidth - hintGutterWidth;
  inset.y = isPortraitInverted ? 50 : 0;
  inset.height = pageHeight - inset.y;
  return inset;
}

ListScreen listScreen(const GfxRenderer& renderer, const bool rotatedInsets) {
  const auto& m = UITheme::getInstance().getMetrics();
  ListScreen screen;
  screen.inset =
      rotatedInsets ? rotatedReaderInsets(renderer)
                    : ContentInset{0, 0, renderer.getScreenWidth(), renderer.getScreenHeight()};
  screen.header = Rect{screen.inset.x, screen.inset.y + m.topPadding, screen.inset.width, m.headerHeight};
  const int bodyTop = screen.header.y + screen.header.height + m.verticalSpacing;
  const int bodyBottom = screen.inset.y + screen.inset.height - m.buttonHintsHeight - m.verticalSpacing;
  screen.body = Rect{screen.inset.x, bodyTop, screen.inset.width, std::max(0, bodyBottom - bodyTop)};
  return screen;
}

void drawBodyMessage(const GfxRenderer& renderer, const Rect& body, const char* message, const bool bold) {
  if (message == nullptr || message[0] == '\0' || body.height <= 0) {
    return;
  }
  const int fontId = bold ? UI_12_FONT_ID : UI_10_FONT_ID;
  const auto style = bold ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR;
  const int lineH = renderer.getLineHeight(fontId);
  const int textY = body.y + (body.height - lineH) / 2;
  renderer.drawCenteredText(fontId, textY, message, true, style);
}

void drawBodyText(const GfxRenderer& renderer, const Rect& body, const char* text, const int fontId,
                  const EpdFontFamily::Style style) {
  if (text == nullptr || text[0] == '\0' || body.width <= 0 || body.height <= 0) {
    return;
  }
  const auto& m = UITheme::getInstance().getMetrics();
  const int pad = m.contentSidePadding;
  const int maxWidth = body.width - pad * 2;
  const int lineHeight = renderer.getLineHeight(fontId);
  auto lines = renderer.wrappedText(fontId, text, maxWidth, 32, style);
  int totalHeight = static_cast<int>(lines.size()) * lineHeight;
  int y = body.y;
  if (totalHeight < body.height) {
    y += (body.height - totalHeight) / 2;
  }
  const int maxY = body.y + body.height - lineHeight;
  for (const auto& line : lines) {
    if (y > maxY) {
      break;
    }
    renderer.drawText(fontId, body.x + pad, y, line.c_str(), true, style);
    y += lineHeight;
  }
}

}  // namespace AppScreenLayout
