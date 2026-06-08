#pragma once

#include <CrossPointSettings.h>
#include <GfxRenderer.h>
#include <HalTiltSensor.h>
#include <Logging.h>

#include "MappedInputManager.h"
#include "util/AppScreenLayout.h"

namespace ReaderUtils {

// Content area that avoids overlap with side/top button hints in rotated reader UIs.
struct ReaderOverlayLayout {
  int contentX = 0;
  int contentY = 0;
  int contentWidth = 0;
  int contentHeight = 0;
};

inline ReaderOverlayLayout readerOverlayLayout(const GfxRenderer& renderer) {
  const AppScreenLayout::ContentInset inset = AppScreenLayout::rotatedReaderInsets(renderer);
  return {inset.x, inset.y, inset.width, inset.height};
}

// Set by screenshot popup on 397; next reader page display uses FULL_REFRESH once.
inline bool forceFullRefreshOnNextDisplay = false;

inline void requestFullRefreshOnNextPage() { forceFullRefreshOnNextDisplay = true; }

constexpr unsigned long GO_HOME_MS = 1000;
constexpr unsigned long SKIP_HOLD_MS = 700;

#if defined(BOARD_ESP32_S3_EPAPER_397)
// SSD1677 + single framebuffer: anti-aliasing pass causes dark/inverted pages and extra flashes.
inline bool readerTextAntiAliasingEnabled() { return false; }
#else
inline bool readerTextAntiAliasingEnabled() { return SETTINGS.textAntiAliasing != 0; }
#endif

inline void applyOrientation(GfxRenderer& renderer, const uint8_t orientation) {
  switch (orientation) {
    case CrossPointSettings::ORIENTATION::PORTRAIT:
      renderer.setOrientation(GfxRenderer::Orientation::Portrait);
      break;
    case CrossPointSettings::ORIENTATION::LANDSCAPE_CW:
      renderer.setOrientation(GfxRenderer::Orientation::LandscapeClockwise);
      break;
    case CrossPointSettings::ORIENTATION::INVERTED:
      renderer.setOrientation(GfxRenderer::Orientation::PortraitInverted);
      break;
    case CrossPointSettings::ORIENTATION::LANDSCAPE_CCW:
      renderer.setOrientation(GfxRenderer::Orientation::LandscapeCounterClockwise);
      break;
    default:
      break;
  }
}

struct PageTurnResult {
  bool prev;
  bool next;
  bool fromTilt;
};

// Short Back (BOOT on 3.97"); legacy boards use timed release.
inline bool wasShortBackClicked(const MappedInputManager& input) {
#if defined(BOARD_ESP32_S3_EPAPER_397)
  return input.wasBackClicked();
#else
  return input.wasReleased(MappedInputManager::Button::Back) && input.getHeldTime() < GO_HOME_MS;
#endif
}

inline PageTurnResult detectPageTurn(const MappedInputManager& input) {
  const bool usePress = SETTINGS.longPressButtonBehavior == SETTINGS.OFF;
  const bool tiltNext = SETTINGS.tiltPageTurn && halTiltSensor.wasTiltedForward();
  const bool tiltPrev = SETTINGS.tiltPageTurn && halTiltSensor.wasTiltedBack();
  const bool prev = tiltPrev || (usePress ? (input.wasPressed(MappedInputManager::Button::PageBack) ||
                                             input.wasPressed(MappedInputManager::Button::Left))
                                          : (input.wasReleased(MappedInputManager::Button::PageBack) ||
                                             input.wasReleased(MappedInputManager::Button::Left)));
  const bool powerTurn = SETTINGS.shortPwrBtn == CrossPointSettings::SHORT_PWRBTN::PAGE_TURN &&
                         input.wasReleased(MappedInputManager::Button::Power);
  const bool next = tiltNext || (usePress ? (input.wasPressed(MappedInputManager::Button::PageForward) || powerTurn ||
                                             input.wasPressed(MappedInputManager::Button::Right))
                                          : (input.wasReleased(MappedInputManager::Button::PageForward) || powerTurn ||
                                             input.wasReleased(MappedInputManager::Button::Right)));
  return {prev, next, tiltPrev || tiltNext};
}

inline void displayWithRefreshCycle(const GfxRenderer& renderer, int& pagesUntilFullRefresh) {
#if defined(BOARD_ESP32_S3_EPAPER_397)
  // SSD1677 (Waveshare 3.97"): HALF_REFRESH uses a waveform that washes gray and ghosts badly compared to X4.
  // Use FAST between periodic FULL clears — matches prior healthy 397 behaviour.
  if (forceFullRefreshOnNextDisplay) {
    forceFullRefreshOnNextDisplay = false;
    renderer.displayBuffer(HalDisplay::FULL_REFRESH);
    pagesUntilFullRefresh = SETTINGS.getRefreshFrequency();
    return;
  }
  if (pagesUntilFullRefresh <= 1) {
    renderer.displayBuffer(HalDisplay::FULL_REFRESH);
    pagesUntilFullRefresh = SETTINGS.getRefreshFrequency();
  } else {
    renderer.displayBuffer(HalDisplay::FAST_REFRESH);
    pagesUntilFullRefresh--;
  }
#else
  if (pagesUntilFullRefresh <= 1) {
    renderer.displayBuffer(HalDisplay::HALF_REFRESH);
    pagesUntilFullRefresh = SETTINGS.getRefreshFrequency();
  } else {
    renderer.displayBuffer();
    pagesUntilFullRefresh--;
  }
#endif
}

// Grayscale anti-aliasing pass. Renders content twice (LSB + MSB) to build
// the grayscale buffer. Only the content callback is re-rendered — status bars
// and other overlays should be drawn before calling this.
// Kept as a template to avoid std::function overhead; instantiated once per reader type.
template <typename RenderFn>
void renderAntiAliased(GfxRenderer& renderer, RenderFn&& renderFn) {
  if (!renderer.storeBwBuffer()) {
    LOG_ERR("READER", "Failed to store BW buffer for anti-aliasing");
    return;
  }

  renderer.clearScreen(0x00);
  renderer.setRenderMode(GfxRenderer::GRAYSCALE_LSB);
  renderFn();
  renderer.copyGrayscaleLsbBuffers();

  renderer.clearScreen(0x00);
  renderer.setRenderMode(GfxRenderer::GRAYSCALE_MSB);
  renderFn();
  renderer.copyGrayscaleMsbBuffers();

  renderer.displayGrayBuffer();
  renderer.setRenderMode(GfxRenderer::BW);

  renderer.restoreBwBuffer();
}

}  // namespace ReaderUtils
