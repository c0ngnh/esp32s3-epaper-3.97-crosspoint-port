#pragma once

#include <HalGPIO.h>

class MappedInputManager {
 public:
  enum class Button { Back, Confirm, Left, Right, Up, Down, Power, PageBack, PageForward };

  struct Labels {
    const char* btn1;
    const char* btn2;
    const char* btn3;
    const char* btn4;
  };

  explicit MappedInputManager(HalGPIO& gpio) : gpio(gpio) {}

  void update() const { gpio.update(); }
#if defined(BOARD_ESP32_S3_EPAPER_397)
  void clearPendingCenterKey() const { gpio.clearPendingConfirmTap(); }
  bool isConfirmSuppressed() const { return gpio.isConfirmSuppressed(); }
  bool consumeConfirmFullRefreshRequest() const { return gpio.consumeConfirmFullRefreshRequest(); }
#endif
  // On 3.97" short BOOT emits synthetic BTN_BACK (not hold-to-back).
  bool wasBackClicked() const;
  bool wasPressed(Button button) const;
  bool wasReleased(Button button) const;
  // Center-key OK for 3.97" (deferred release); safe to call once per loop iteration.
  bool wasConfirmClicked() const;
  bool isPressed(Button button) const;
  bool wasAnyPressed() const;
  bool wasAnyReleased() const;
  unsigned long getHeldTime() const;
  Labels mapLabels(const char* back, const char* confirm, const char* previous, const char* next) const;
  // Returns the raw front button index that was pressed this frame (or -1 if none).
  int getPressedFrontButton() const;

 private:
  HalGPIO& gpio;

  bool mapButton(Button button, bool (HalGPIO::*fn)(uint8_t) const) const;
};
