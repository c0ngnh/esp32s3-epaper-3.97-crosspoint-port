#include "ConfirmationActivity.h"

#include <I18n.h>

#include "HalDisplay.h"
#include "HalGPIO.h"
#include "components/UITheme.h"
#include "util/AppScreenLayout.h"

ConfirmationActivity::ConfirmationActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                           const std::string& heading, const std::string& body,
                                           const bool showOkBackDeleteHints)
    : Activity("Confirmation", renderer, mappedInput),
      heading(heading),
      body(body),
      showOkBackDeleteHints(showOkBackDeleteHints) {}

void ConfirmationActivity::onEnter() {
  Activity::onEnter();

  lineHeight = renderer.getLineHeight(fontId);
  const auto screen = AppScreenLayout::listScreen(renderer);
  const int maxWidth = screen.body.width - UITheme::getInstance().getMetrics().contentSidePadding * 2;

  if (!heading.empty()) {
    safeHeading = renderer.truncatedText(fontId, heading.c_str(), maxWidth, EpdFontFamily::BOLD);
  }
  if (!body.empty()) {
    safeBody = renderer.truncatedText(fontId, body.c_str(), maxWidth, EpdFontFamily::REGULAR);
  }

  requestUpdate(true);
}

void ConfirmationActivity::render(RenderLock&& lock) {
  renderer.clearScreen();
  const auto screen = AppScreenLayout::listScreen(renderer);

  if (!safeHeading.empty()) {
    GUI.drawHeader(renderer, screen.header, safeHeading.c_str());
  }

#if defined(BOARD_ESP32_S3_EPAPER_397)
  const bool drawDeleteHints = showOkBackDeleteHints;
#else
  const bool drawDeleteHints = false;
#endif
  const int hintLineH = renderer.getLineHeight(fontId);
  const int hintBlockH = drawDeleteHints ? hintLineH * 2 + 12 : 0;
  Rect contentBody = screen.body;
  if (hintBlockH > 0 && contentBody.height > hintBlockH) {
    contentBody.height -= hintBlockH;
  }

  if (!safeBody.empty()) {
    AppScreenLayout::drawBodyText(renderer, contentBody, safeBody.c_str(), fontId, EpdFontFamily::REGULAR);
  } else if (!safeHeading.empty()) {
    AppScreenLayout::drawBodyMessage(renderer, contentBody, safeHeading.c_str(), true);
  }

  if (drawDeleteHints) {
    const int hintsY = screen.body.y + screen.body.height - hintBlockH + 4;
    renderer.drawCenteredText(fontId, hintsY, tr(STR_DELETE_SHAKE_HINT_OK), true, EpdFontFamily::REGULAR);
    renderer.drawCenteredText(fontId, hintsY + hintLineH + 4, tr(STR_DELETE_SHAKE_HINT_BACK), true,
                              EpdFontFamily::REGULAR);
  }

#if defined(BOARD_ESP32_S3_EPAPER_397)
  const auto labels = mappedInput.mapLabels(I18N.get(StrId::STR_CANCEL), I18N.get(StrId::STR_CONFIRM), "", "");
#else
  const auto labels = mappedInput.mapLabels("", "", I18N.get(StrId::STR_CANCEL), I18N.get(StrId::STR_CONFIRM));
#endif
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}

void ConfirmationActivity::loop() {
#if defined(BOARD_ESP32_S3_EPAPER_397)
  if (consumeConfirmClick()) {
    ActivityResult res;
    res.isCancelled = false;
    setResult(std::move(res));
    finish();
    return;
  }

  if (mappedInput.wasBackClicked()) {
    ActivityResult res;
    res.isCancelled = true;
    setResult(std::move(res));
    finish();
    return;
  }
#else
  if (mappedInput.wasReleased(MappedInputManager::Button::Right)) {
    ActivityResult res;
    res.isCancelled = false;
    setResult(std::move(res));
    finish();
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Left)) {
    ActivityResult res;
    res.isCancelled = true;
    setResult(std::move(res));
    finish();
    return;
  }
#endif
}
