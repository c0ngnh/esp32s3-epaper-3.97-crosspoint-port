#pragma once

#include <GfxRenderer.h>
#include <functional>
#include <string>

#include "components/themes/BaseTheme.h"

namespace AppScreenLayout {

// Safe content area when button hints sit on a side edge or top (rotated reader).
struct ContentInset {
  int x = 0;
  int y = 0;
  int width = 0;
  int height = 0;
};

struct ListScreen {
  ContentInset inset{};
  Rect header{};
  Rect body{};
};

ContentInset rotatedReaderInsets(const GfxRenderer& renderer);
ListScreen listScreen(const GfxRenderer& renderer, bool rotatedInsets = false);

// Vertically centered message in the body band (loading, errors, empty states).
void drawBodyMessage(const GfxRenderer& renderer, const Rect& body, const char* message, bool bold = false);

// Wrapped paragraphs below optional header, above button hints.
void drawBodyText(const GfxRenderer& renderer, const Rect& body, const char* text, int fontId,
                  EpdFontFamily::Style style = EpdFontFamily::REGULAR);

}  // namespace AppScreenLayout
