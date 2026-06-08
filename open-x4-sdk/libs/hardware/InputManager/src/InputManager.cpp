#include "InputManager.h"

#if defined(BOARD_ESP32_S3_EPAPER_397)
static constexpr unsigned long CONFIRM_FULL_REFRESH_MS = 2500;
#endif

// Recorded ADC values from real devices
// BACK CONF LEFT RGHT   UP DOWN
// 3597 2760 1530    6 2300    6
// 3470 2666 1480    6 2222    5
// 3470 2655 1470    3 2205    3

// Averages
// BACK CONF LEFT RGHT   UP DOWN
// 3512 2694 1493    5 2242    5

// Setup ranges, if ADC value is between value `i` and `i + 1`, button `i` is being pressed
// These ranges are based on real world values above, and are much more tolerant of different
// devices than a fixed threshold check
// These values are calculated by taking the midpoint of the pairs of averaged values above
const int InputManager::ADC_RANGES_1[] = {ADC_NO_BUTTON, 3100, 2090, 750, INT32_MIN};
const int InputManager::ADC_RANGES_2[] = {ADC_NO_BUTTON, 1120, INT32_MIN};
const char* InputManager::BUTTON_NAMES[] = {"Back", "Confirm", "Left", "Right", "Up", "Down", "Power"};

InputManager::InputManager()
    : currentState(0),
      lastState(0),
      pressedEvents(0),
      releasedEvents(0),
      lastDebounceTime(0),
      buttonPressStart(0),
      buttonPressFinish(0) {}

void InputManager::begin() {
#if defined(BOARD_ESP32_S3_EPAPER_397)
  pinMode(BUTTON_UP_PIN, INPUT_PULLUP);
  pinMode(BUTTON_FUNCTION_PIN, INPUT_PULLUP);
  pinMode(BUTTON_DOWN_PIN, INPUT_PULLUP);
  pinMode(BOOT_BUTTON_PIN, INPUT_PULLUP);
#else
  pinMode(BUTTON_ADC_PIN_1, INPUT);
  pinMode(BUTTON_ADC_PIN_2, INPUT);
  pinMode(POWER_BUTTON_PIN, INPUT_PULLUP);
  analogSetAttenuation(ADC_11db);
#endif
}

int InputManager::getButtonFromADC(const int adcValue, const int ranges[], const int numButtons) {
  for (int i = 0; i < numButtons; i++) {
    if (ranges[i + 1] < adcValue && adcValue <= ranges[i]) {
      return i;
    }
  }

  return -1;
}

uint8_t InputManager::getState() {
  uint8_t state = 0;

#if defined(BOARD_ESP32_S3_EPAPER_397)
  // Digital button matrix on the Waveshare S3 ePaper 3.97 board
  if (digitalRead(BUTTON_UP_PIN) == LOW) {
    state |= (1 << BTN_UP);
  }
  if (digitalRead(BUTTON_FUNCTION_PIN) == LOW) {
    state |= (1 << BTN_CONFIRM);
  }
  if (digitalRead(BUTTON_DOWN_PIN) == LOW) {
    state |= (1 << BTN_DOWN);
  }

  // PWR (AXP2101 / GPIO38) is unreliable for tap detection — not wired into button state.
  // GPIO0 BOOT is polled separately in update() (short = Back, hold 3s+ = sleep).
#else
  // Read GPIO1 buttons
  const int adcValue1 = analogRead(BUTTON_ADC_PIN_1);
  const int button1 = getButtonFromADC(adcValue1, ADC_RANGES_1, NUM_BUTTONS_1);
  if (button1 >= 0) {
    state |= (1 << button1);
  }

  // Read GPIO2 buttons
  const int adcValue2 = analogRead(BUTTON_ADC_PIN_2);
  const int button2 = getButtonFromADC(adcValue2, ADC_RANGES_2, NUM_BUTTONS_2);
  if (button2 >= 0) {
    state |= (1 << (button2 + 4));
  }

  // Read power button (digital, active LOW)
  if (digitalRead(POWER_BUTTON_PIN) == LOW) {
    state |= (1 << BTN_POWER);
  }
#endif

  return state;
}

#if defined(BOARD_ESP32_S3_EPAPER_397)
void InputManager::emitSyntheticBack(const unsigned long currentTime) {
  _tapArmed = false;
  _suppressConfirmUntilMs = currentTime + CONFIRM_SUPPRESS_AFTER_GESTURE_MS;
  releasedEvents |= (1 << BTN_BACK);
}

void InputManager::deliverConfirmClick(const unsigned long currentTime) {
  if (_confirmDeliveredThisArm) {
    return;
  }
  if (currentTime < _suppressConfirmUntilMs) {
    return;
  }
  _confirmDeliveredThisArm = true;
  _confirmReleaseLatch = true;
}
#endif

void InputManager::update() {
  const unsigned long currentTime = millis();
  const uint8_t state = getState();

  // Always clear events first
  pressedEvents = 0;
  releasedEvents = 0;

  // Debounce
  if (state != lastState) {
    lastDebounceTime = currentTime;
    lastState = state;
  }

  if ((currentTime - lastDebounceTime) > DEBOUNCE_DELAY) {
    if (state != currentState) {
      // Calculate pressed and released events
      pressedEvents = state & ~currentState;
      releasedEvents = currentState & ~state;

      // If pressing buttons and wasn't before, start recording time
      if (pressedEvents > 0 && currentState == 0) {
        buttonPressStart = currentTime;
      }

      // If releasing a button and no other buttons being pressed, record finish time
      if (releasedEvents > 0 && state == 0) {
        buttonPressFinish = currentTime;
      }

      currentState = state;
    }
  }

#if defined(BOARD_ESP32_S3_EPAPER_397)
  _confirmReleaseLatch = false;

  const bool confirmDown = (state & (1 << BTN_CONFIRM)) != 0;
  const bool suppressConfirm = currentTime < _suppressConfirmUntilMs;

  if (confirmDown && !_confirmWasDown) {
    pressedEvents &= static_cast<uint8_t>(~(1 << BTN_CONFIRM));
    _tapArmed = true;
    _confirmDeliveredThisArm = false;
    _confirmFullRefreshFiredThisArm = false;
    buttonPressStart = currentTime;
  } else if (confirmDown && _confirmWasDown) {
    const unsigned long heldMs = currentTime - buttonPressStart;
    const bool bootDown = digitalRead(BOOT_BUTTON_PIN) == LOW;
    if (heldMs >= CONFIRM_FULL_REFRESH_MS && !_confirmFullRefreshFiredThisArm && !bootDown) {
      _confirmFullRefreshFiredThisArm = true;
      _confirmFullRefreshPending = true;
      _tapArmed = false;
      _confirmDeliveredThisArm = true;
    }
  } else if (!confirmDown && _confirmWasDown) {
    releasedEvents &= static_cast<uint8_t>(~(1 << BTN_CONFIRM));
    if (_tapArmed) {
      _tapArmed = false;
      const bool bootDown = digitalRead(BOOT_BUTTON_PIN) == LOW;
      if (!suppressConfirm && !_confirmFullRefreshFiredThisArm && !bootDown) {
        deliverConfirmClick(currentTime);
      }
    }
    _confirmFullRefreshFiredThisArm = false;
  }

  if (suppressConfirm) {
    pressedEvents &= static_cast<uint8_t>(~(1 << BTN_CONFIRM));
    releasedEvents &= static_cast<uint8_t>(~(1 << BTN_CONFIRM));
  }

  _confirmWasDown = confirmDown;

  // GPIO0 BOOT: short press = Back; hold 3s+ = sleep (main loop).
  static constexpr unsigned long BOOT_SLEEP_MS = 3000;
  static constexpr unsigned long BOOT_SHORT_MAX_MS = 950;
  const bool bootDown = digitalRead(BOOT_BUTTON_PIN) == LOW;
  if (bootDown && !_bootWasDown) {
    _bootPressMs = currentTime;
    _bootSleepEvent = false;
    _confirmHeldDuringBootArm = confirmDown;
    pressedEvents |= (1 << BTN_BOOT);
    _tapArmed = false;
  } else if (bootDown && _bootWasDown) {
    if (confirmDown) {
      _confirmHeldDuringBootArm = true;
    }
    if (!_bootSleepEvent && (currentTime - _bootPressMs) >= BOOT_SLEEP_MS && !confirmDown) {
      _bootSleepEvent = true;
    }
  } else if (!bootDown && _bootWasDown) {
    releasedEvents |= (1 << BTN_BOOT);
    const unsigned long heldMs = currentTime - _bootPressMs;
    if (!_bootSleepEvent && !_confirmHeldDuringBootArm && heldMs > DEBOUNCE_DELAY && heldMs < BOOT_SHORT_MAX_MS) {
      emitSyntheticBack(currentTime);
    }
    _confirmHeldDuringBootArm = false;
  }
  _bootWasDown = bootDown;
#endif
}

bool InputManager::isPressed(const uint8_t buttonIndex) const {
  return currentState & (1 << buttonIndex);
}

bool InputManager::wasPressed(const uint8_t buttonIndex) const {
  return pressedEvents & (1 << buttonIndex);
}

bool InputManager::wasAnyPressed() const {
  return pressedEvents > 0;
}

bool InputManager::wasReleased(const uint8_t buttonIndex) const {
#if defined(BOARD_ESP32_S3_EPAPER_397)
  if (buttonIndex == BTN_CONFIRM) {
    if (_confirmReleaseLatch) {
      _confirmReleaseLatch = false;
      return true;
    }
    return false;
  }
#endif
  return releasedEvents & (1 << buttonIndex);
}

bool InputManager::wasAnyReleased() const {
  return releasedEvents > 0;
}

unsigned long InputManager::getHeldTime() const {
  // Still hold a button
  if (currentState > 0) {
    return millis() - buttonPressStart;
  }

  return buttonPressFinish - buttonPressStart;
}

const char* InputManager::getButtonName(const uint8_t buttonIndex) {
  if (buttonIndex <= BTN_POWER) {
    return BUTTON_NAMES[buttonIndex];
  }
  return "Unknown";
}

bool InputManager::isPowerButtonPressed() const {
  return isPressed(BTN_POWER);
}

#if defined(BOARD_ESP32_S3_EPAPER_397)
void InputManager::clearPendingConfirmTap() {
  _tapArmed = false;
  _confirmReleaseLatch = false;
  _confirmDeliveredThisArm = true;
}

void InputManager::discardNavigationConfirm() { clearPendingConfirmTap(); }

void InputManager::suppressConfirmAfterGesture() {
  _tapArmed = false;
  _confirmDeliveredThisArm = true;
  _confirmReleaseLatch = false;
  _suppressConfirmUntilMs = millis() + CONFIRM_SUPPRESS_AFTER_GESTURE_MS;
}

bool InputManager::isConfirmSuppressed() const { return millis() < _suppressConfirmUntilMs; }

bool InputManager::consumeBootSleepRequest() {
  const bool val = _bootSleepEvent;
  _bootSleepEvent = false;
  return val;
}

bool InputManager::consumeConfirmFullRefreshRequest() {
  const bool pending = _confirmFullRefreshPending;
  _confirmFullRefreshPending = false;
  return pending;
}

bool InputManager::isBootPressed() const { return _bootWasDown; }
#endif
