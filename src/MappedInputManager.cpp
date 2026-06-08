#include "MappedInputManager.h"

#include "CrossPointSettings.h"

namespace {
using ButtonIndex = uint8_t;

struct SideLayoutMap {
  ButtonIndex pageBack;
  ButtonIndex pageForward;
};

// Order matches CrossPointSettings::SIDE_BUTTON_LAYOUT.
constexpr SideLayoutMap kSideLayouts[] = {
    {HalGPIO::BTN_UP, HalGPIO::BTN_DOWN},
    {HalGPIO::BTN_DOWN, HalGPIO::BTN_UP},
};
}  // namespace

bool MappedInputManager::mapButton(const Button button, bool (HalGPIO::*fn)(uint8_t) const) const {
  const auto sideLayout = static_cast<CrossPointSettings::SIDE_BUTTON_LAYOUT>(SETTINGS.sideButtonLayout);
  const auto& side = kSideLayouts[sideLayout];

  switch (button) {
    case Button::Back:
#if defined(BOARD_ESP32_S3_EPAPER_397)
      return (gpio.*fn)(HalGPIO::BTN_BACK);
#else
      return (gpio.*fn)(SETTINGS.frontButtonBack);
#endif
    case Button::Confirm:
#if defined(BOARD_ESP32_S3_EPAPER_397)
      return (gpio.*fn)(HalGPIO::BTN_CONFIRM);
#else
      return (gpio.*fn)(SETTINGS.frontButtonConfirm);
#endif
    case Button::Left:
#if defined(BOARD_ESP32_S3_EPAPER_397)
      return (gpio.*fn)(HalGPIO::BTN_UP);
#else
      return (gpio.*fn)(SETTINGS.frontButtonLeft);
#endif
    case Button::Right:
#if defined(BOARD_ESP32_S3_EPAPER_397)
      return (gpio.*fn)(HalGPIO::BTN_DOWN);
#else
      return (gpio.*fn)(SETTINGS.frontButtonRight);
#endif
    case Button::Up:
      // Side buttons remain fixed for Up/Down.
      return (gpio.*fn)(HalGPIO::BTN_UP);
    case Button::Down:
      // Side buttons remain fixed for Up/Down.
      return (gpio.*fn)(HalGPIO::BTN_DOWN);
    case Button::Power:
      // Power button bypasses remapping.
      return (gpio.*fn)(HalGPIO::BTN_POWER);
    case Button::PageBack:
      // Reader page navigation uses side buttons and can be swapped via settings.
      return (gpio.*fn)(side.pageBack);
    case Button::PageForward:
      // Reader page navigation uses side buttons and can be swapped via settings.
      return (gpio.*fn)(side.pageForward);
  }

  return false;
}

bool MappedInputManager::wasPressed(const Button button) const {
#if defined(BOARD_ESP32_S3_EPAPER_397)
  if (button == Button::Back) {
    return gpio.wasPressed(HalGPIO::BTN_BACK);
  }
  if (button == Button::Confirm && gpio.isConfirmSuppressed()) {
    return false;
  }
#endif
  return mapButton(button, &HalGPIO::wasPressed);
}

bool MappedInputManager::wasReleased(const Button button) const {
#if defined(BOARD_ESP32_S3_EPAPER_397)
  if (button == Button::Back) {
    return gpio.wasReleased(HalGPIO::BTN_BACK);
  }
  if (button == Button::Confirm && gpio.isConfirmSuppressed()) {
    return false;
  }
#endif
  return mapButton(button, &HalGPIO::wasReleased);
}

bool MappedInputManager::isPressed(const Button button) const {
  return mapButton(button, &HalGPIO::isPressed);
}

bool MappedInputManager::wasConfirmClicked() const {
#if defined(BOARD_ESP32_S3_EPAPER_397)
  if (gpio.isConfirmSuppressed()) {
    return false;
  }
  return gpio.wasReleased(HalGPIO::BTN_CONFIRM);
#else
  return wasReleased(Button::Confirm);
#endif
}

bool MappedInputManager::wasBackClicked() const {
#if defined(BOARD_ESP32_S3_EPAPER_397)
  // Short BOOT press (synthetic BTN_BACK). Hold center 1s+ is go-home in reader, not Back here.
  return gpio.wasReleased(HalGPIO::BTN_BACK) || gpio.wasPressed(HalGPIO::BTN_BACK);
#else
  return wasPressed(Button::Back);
#endif
}

bool MappedInputManager::wasAnyPressed() const { return gpio.wasAnyPressed(); }

bool MappedInputManager::wasAnyReleased() const { return gpio.wasAnyReleased(); }

unsigned long MappedInputManager::getHeldTime() const { return gpio.getHeldTime(); }

MappedInputManager::Labels MappedInputManager::mapLabels(const char* back, const char* confirm, const char* previous,
                                                         const char* next) const {
#if defined(BOARD_ESP32_S3_EPAPER_397)
  // Bottom bar hints hidden on 397; center tap = OK; short BOOT = Back.
  (void)SETTINGS;
  (void)confirm;
  return {back, "", previous, next};
#endif
  auto labelForHardware = [&](uint8_t hw) -> const char* {
    // Compare against configured logical roles and return the matching label.
    if (hw == SETTINGS.frontButtonBack) {
      return back;
    }
    if (hw == SETTINGS.frontButtonConfirm) {
      return confirm;
    }
    if (hw == SETTINGS.frontButtonLeft) {
      return previous;
    }
    if (hw == SETTINGS.frontButtonRight) {
      return next;
    }
    return "";
  };

  return {labelForHardware(HalGPIO::BTN_BACK), labelForHardware(HalGPIO::BTN_CONFIRM),
          labelForHardware(HalGPIO::BTN_LEFT), labelForHardware(HalGPIO::BTN_RIGHT)};
}

int MappedInputManager::getPressedFrontButton() const {
  // Scan the raw front buttons in hardware order.
  // This bypasses remapping so the remap activity can capture physical presses.
  if (gpio.wasPressed(HalGPIO::BTN_BACK)) {
    return HalGPIO::BTN_BACK;
  }
  if (gpio.wasPressed(HalGPIO::BTN_CONFIRM)) {
    return HalGPIO::BTN_CONFIRM;
  }
  if (gpio.wasPressed(HalGPIO::BTN_LEFT)) {
    return HalGPIO::BTN_LEFT;
  }
  if (gpio.wasPressed(HalGPIO::BTN_RIGHT)) {
    return HalGPIO::BTN_RIGHT;
  }
  return -1;
}