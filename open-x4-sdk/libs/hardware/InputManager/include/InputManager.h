#pragma once

#include <Arduino.h>

class InputManager {
 public:
  InputManager();
  void begin();
  uint8_t getState();

  /**
   * Updates the button states. Should be called regularly in the main loop.
   */
  void update();

  /**
   * Returns true if the button was being held at the time of the last #update() call.
   *
   * @param buttonIndex the button indexes
   * @return the button current press state
   */
  bool isPressed(uint8_t buttonIndex) const;

 /**
   * Returns true if the button went from unpressed to pressed between the last two #update() calls.
   *
   * This differs from #isPressed() in that pressing and holding a button will cause this function
   * to return true after the first #update() call, but false on subsequent calls, whereas #isPressed()
   * will continue to return true.
   *
   * @param buttonIndex
   * @return the button pressed state
   */
  bool wasPressed(uint8_t buttonIndex) const;

  /**
   * Returns true if any button started being pressed between the last two #update() calls
   *
   * @return true if any button started being pressed between the last two #update() calls
   */
  bool wasAnyPressed() const;

  /**
   * Returns true if the button went from pressed to unpressed between the last two #update() calls
   *
   * @param buttonIndex the button indexes
   * @return the button release state
   */
  bool wasReleased(uint8_t buttonIndex) const;

  /**
   * Returns true if any button was released between the last two #update() calls
   *
   * @return  true if any button was released between the last two #update() calls
   */
  bool wasAnyReleased() const;

  /**
   * Returns the time between any button starting to be depressed and all buttons between released
   *
   * @return duration in milliseconds
   */
  unsigned long getHeldTime() const;

  // Button indices
  static constexpr uint8_t BTN_BACK = 0;
  static constexpr uint8_t BTN_CONFIRM = 1;
  static constexpr uint8_t BTN_LEFT = 2;
  static constexpr uint8_t BTN_RIGHT = 3;
  static constexpr uint8_t BTN_UP = 4;
  static constexpr uint8_t BTN_DOWN = 5;
  static constexpr uint8_t BTN_POWER = 6;
#if defined(BOARD_ESP32_S3_EPAPER_397)
  static constexpr uint8_t BTN_BOOT = 7;
#endif

  // Pins
#if defined(BOARD_ESP32_S3_EPAPER_397)
  static constexpr int BUTTON_UP_PIN = 4;
  static constexpr int BUTTON_FUNCTION_PIN = 5;
  static constexpr int BUTTON_DOWN_PIN = 6;
  // GPIO0 is BOOT (download). PWR key is on AXP2101 PEK — polled via HalBoard397.
  static constexpr int BOOT_BUTTON_PIN = 0;
#else
  static constexpr int BUTTON_ADC_PIN_1 = 1;
  static constexpr int BUTTON_ADC_PIN_2 = 2;
  static constexpr int POWER_BUTTON_PIN = 3;
#endif

  // Power button methods
  bool isPowerButtonPressed() const;

#if defined(BOARD_ESP32_S3_EPAPER_397)
  // Block stray OK briefly after BOOT-Back or IMU gesture.
  static constexpr unsigned long CONFIRM_SUPPRESS_AFTER_GESTURE_MS = 400;

  // Cancel in-flight center tap (e.g. activity enter).
  void clearPendingConfirmTap();
  // On activity enter: drop any armed center tap for a fresh screen.
  void discardNavigationConfirm();
  // After IMU gesture: block stray OK briefly.
  void suppressConfirmAfterGesture();
  bool isConfirmSuppressed() const;
  // Long hold GPIO0 BOOT (>= 3s) requests deep sleep (consumed once).
  bool consumeBootSleepRequest();
  bool isBootPressed() const;
  // Select/Function held >= 2.5s: request one full-panel refresh (consumed once).
  bool consumeConfirmFullRefreshRequest();
#endif

  // Button names
  static const char* getButtonName(uint8_t buttonIndex);

 private:
  int getButtonFromADC(int adcValue, const int ranges[], int numButtons);

  uint8_t currentState;
  uint8_t lastState;
  uint8_t pressedEvents;
  uint8_t releasedEvents;
  unsigned long lastDebounceTime;
  unsigned long buttonPressStart;
  unsigned long buttonPressFinish;

#if defined(BOARD_ESP32_S3_EPAPER_397)
  bool _confirmWasDown = false;
  bool _tapArmed = false;
  unsigned long _suppressConfirmUntilMs = 0;
  bool _confirmDeliveredThisArm = false;
  bool _confirmFullRefreshFiredThisArm = false;
  bool _confirmFullRefreshPending = false;
  mutable bool _confirmReleaseLatch = false;
  bool _bootWasDown = false;
  unsigned long _bootPressMs = 0;
  bool _bootSleepEvent = false;
  bool _confirmHeldDuringBootArm = false;

  void deliverConfirmClick(unsigned long currentTime);
  void emitSyntheticBack(unsigned long currentTime);
#endif

  static constexpr int NUM_BUTTONS_1 = 4;
  static const int ADC_RANGES_1[];

  static constexpr int NUM_BUTTONS_2 = 2;
  static const int ADC_RANGES_2[];

  static constexpr int ADC_NO_BUTTON = 3800;
#if defined(BOARD_ESP32_S3_EPAPER_397)
  static constexpr unsigned long DEBOUNCE_DELAY = 30;
#else
  static constexpr unsigned long DEBOUNCE_DELAY = 5;
#endif

  static const char* BUTTON_NAMES[];
};
