#pragma once

#include <Arduino.h>
#include <Wire.h>

#include "HalGPIO.h"

// TODO: Move enums into new header and share with CrossPointSettings.h
namespace CrossPointOrientation {
enum Value : uint8_t { PORTRAIT = 0, LANDSCAPE_CW = 1, INVERTED = 2, LANDSCAPE_CCW = 3 };
}

namespace CrossPointTiltPageTurn {
enum Value : uint8_t { TILT_OFF = 0, TILT_NORMAL = 1, TILT_INVERTED = 2 };
}

class HalTiltSensor;
extern HalTiltSensor halTiltSensor;  // Singleton

class HalTiltSensor {
 public:
  enum class KeyboardTiltDir : uint8_t { None, Left, Right, Up, Down };

 private:
  bool _available = false;
  uint8_t _i2cAddr = 0;

  // Keyboard (gyroscope flick) navigation — Waveshare 397 three-button UI
  bool _keyboardNavActive = false;
  KeyboardTiltDir _keyboardTiltEvent = KeyboardTiltDir::None;
  bool _keyboardInTiltPitch = false;
  bool _keyboardInTiltRoll = false;
  unsigned long _keyboardLastTiltMs = 0;
  unsigned long _keyboardLastPollMs = 0;
  unsigned long _keyboardWakeMs = 0;
  KeyboardTiltDir _keyboardSuppressOppositeDir = KeyboardTiltDir::None;
  unsigned long _keyboardSuppressOppositeUntilMs = 0;

  // Flick gesture (gyro angular rate). Tuned for QMI8658 on Waveshare 397 portrait grip.
  static constexpr float KB_RATE_THRESHOLD_DPS = 180.0f;
  static constexpr float KB_NEUTRAL_RATE_DPS = 85.0f;
  static constexpr unsigned long KB_COOLDOWN_MS = 550;
  static constexpr unsigned long KB_OPPOSITE_SUPPRESS_MS = 850;
  static constexpr unsigned long KB_POLL_INTERVAL_MS = 30;
  static constexpr unsigned long KB_WAKE_STABILIZE_MS = 400;

  // Tilt gesture state machine
  bool _tiltForwardEvent = false;  // Consumed by wasTiltedForward()
  bool _tiltBackEvent = false;     // Consumed by wasTiltedBack()
  bool _hadActivity = false;       // Non-consuming flag for sleep timer
  bool _inTilt = false;            // Currently tilted past threshold
  bool _isAwake = false;           // Tracks power state
  unsigned long _initMs = 0;       // Timestamp of sensor init
  unsigned long _lastTiltMs = 0;   // Debounce / cooldown
  unsigned long _wakeMs = 0;       // Timestamp of last wake() for stabilization

  // Tuning constants
  static constexpr float RATE_THRESHOLD_DPS = 270.0f;      // Deg/sec speed to trigger flick
  static constexpr float NEUTRAL_RATE_DPS = 50.0f;         // Must stop moving below this rate before next trigger
  static constexpr unsigned long COOLDOWN_MS = 600;        // Minimum ms between triggers
  static constexpr unsigned long POLL_INTERVAL_MS = 50;    // 20 Hz polling
  static constexpr unsigned long WAKE_STABILIZE_MS = 300;  // Ignore readings after wake
  static constexpr unsigned long SLEEP_STABILIZE_MS = 15;  // Sleep turn on/off delay

  mutable unsigned long _lastPollMs = 0;

  // App gestures (Waveshare 397): hard-shake delete, flip-180-return (music random)
  bool _appGesturesEnabled = false;
  uint8_t _appGestureRefCount = 0;
  bool _suspendedForI2c = false;
  bool _shakeDeleteEvent = false;
  bool _shakeBookmarkEvent = false;
  bool _flip180ReturnEvent = false;
  unsigned long _lastShakeMs = 0;
  unsigned long _lastBookmarkShakeMs = 0;
  unsigned long _lastFlipMs = 0;
  unsigned long _lastAppPollMs = 0;
  uint8_t _shakeStrikes = 0;
  unsigned long _shakeStrikeWindowMs = 0;
  uint8_t _bookmarkShakeStrikes = 0;
  unsigned long _bookmarkShakeStrikeWindowMs = 0;
  enum class FlipState : uint8_t { Normal, InvertedPending, Inverted, RestoringPending };
  FlipState _flipState = FlipState::Normal;
  unsigned long _flipStateMs = 0;

  static constexpr float SHAKE_THRESHOLD_G = 3.6f;
  static constexpr float SHAKE_JERK_G = 1.8f;
  static constexpr unsigned long SHAKE_COOLDOWN_MS = 4000;
  static constexpr uint8_t SHAKE_STRIKES_REQUIRED = 3;
  static constexpr unsigned long SHAKE_STRIKE_WINDOW_MS = 500;
  // Bookmark uses a stricter profile so walking/stairs do not trigger it.
  static constexpr float SHAKE_BOOKMARK_THRESHOLD_G = 4.8f;
  static constexpr float SHAKE_BOOKMARK_JERK_G = 2.4f;
  static constexpr unsigned long SHAKE_BOOKMARK_COOLDOWN_MS = 5000;
  static constexpr uint8_t SHAKE_BOOKMARK_STRIKES_REQUIRED = 4;
  static constexpr unsigned long SHAKE_BOOKMARK_STRIKE_WINDOW_MS = 650;
  float _shakeBaselineG = 1.0f;
  bool _shakeDeleteEnabled = true;
  bool _flipRelaxed = false;
  static constexpr unsigned long APP_GESTURE_POLL_MS = 40;
#if defined(BOARD_ESP32_S3_EPAPER_397)
  static constexpr float FLIP_INVERTED_G = -0.82f;
  static constexpr float FLIP_NORMAL_G = 0.82f;
  static constexpr float FLIP_STABLE_G_MIN = 0.88f;
  static constexpr float FLIP_STABLE_G_MAX = 1.12f;
  static constexpr unsigned long FLIP_HOLD_MS = 700;
  static constexpr unsigned long FLIP_COOLDOWN_MS = 4500;
#else
  static constexpr float FLIP_INVERTED_G = -0.6f;
  static constexpr float FLIP_NORMAL_G = 0.6f;
  static constexpr float FLIP_STABLE_G_MIN = 0.0f;
  static constexpr float FLIP_STABLE_G_MAX = 99.0f;
  static constexpr unsigned long FLIP_HOLD_MS = 350;
  static constexpr unsigned long FLIP_COOLDOWN_MS = 2000;
#endif

  float dominantGravityAxis(float ax, float ay, float az) const;
  void pollAppGestures(unsigned long now);

  // --- QMI8658 registers ---
  static constexpr uint8_t REG_CTRL1 = 0x02;
  static constexpr uint8_t REG_CTRL2 = 0x03;
  static constexpr uint8_t REG_CTRL3 = 0x04;
  static constexpr uint8_t REG_CTRL7 = 0x08;
  static constexpr uint8_t REG_AX_L = 0x35;
  static constexpr uint8_t REG_GX_L = 0x3B;

  // --- Register Bit Flags ---

  // REG_CTRL1 (0x02)
  static constexpr uint8_t CTRL1_BIG_ENDIAN = (1 << 5);      // 0x20: 1 = Big Endian 16-bit data
  static constexpr uint8_t CTRL1_AUTO_INC = (1 << 6);        // 0x40: Enable address auto-increment
  static constexpr uint8_t CTRL1_SENSOR_DISABLE = (1 << 0);  // 0x01: Power down sensor engine
  // readAccel/readGyro reorder bytes as little-endian (lo first); CTRL1 must NOT set Big-Endian bit.
  static constexpr uint8_t CTRL1_BASE = CTRL1_AUTO_INC;  // 0x40

  // REG_CTRL2 (0x03) - Accel config
  static constexpr uint8_t CTRL2_FS_4G = (0b001 << 4);  // ±4g
  static constexpr uint8_t CTRL2_ODR_28HZ = 0b0110;    // ~28 Hz

  // REG_CTRL3 (0x04) - Gyro Config
  static constexpr uint8_t CTRL3_FS_512DPS = (0b101 << 4);  // Bits 6:4 = 101
  static constexpr uint8_t CTRL3_ODR_28HZ = 0b1000;         // Bits 3:0 = 1000 (28.025 Hz)

  // REG_CTRL7 (0x08) - Enable
  static constexpr uint8_t CTRL7_DISABLE_ALL = 0x00;
  static constexpr uint8_t CTRL7_ACCEL_ENABLE = (1 << 0);
  static constexpr uint8_t CTRL7_GYRO_ENABLE = (1 << 1);

  bool writeReg(uint8_t reg, uint8_t val) const;
  bool readReg(uint8_t reg, uint8_t* val) const;
  bool readGyro(float& gx, float& gy, float& gz) const;
  bool readAccel(float& ax, float& ay, float& az) const;
  bool wakeAccelOnly();
  // Disable sensors for shared I2C (no stabilize wait); used by ImuBusPause.
  bool releaseBus();

 public:
  // Call after gpio.begin() and powerManager.begin() (I2C already initialised for X3)
  void begin();

  // Enables the QMI8658 internal sensor engine
  bool wake();

  // Puts the QMI8658 into a low-power standby state
  bool deepSleep(bool force = false);

  // True if the QMI8658 IMU is present on this device
  bool isAvailable() const { return _available; }

  // Poll the accelerometer and update tilt gesture state.
  void update(const uint8_t mode, const uint8_t orientation, const bool inReader);

  // Returns true once per tilt-forward gesture (next page direction).
  // Consumed on read — subsequent calls return false until next gesture.
  bool wasTiltedForward();

  // Returns true once per tilt-back gesture (previous page direction).
  // Consumed on read.
  bool wasTiltedBack();

  // Non-consuming: true if any tilt activity occurred since last call.
  // Used to reset the auto-sleep inactivity timer.
  bool hadActivity();

  // Discard any pending tilt events (call when leaving reader or disabling tilt).
  void clearPendingEvents();

  // Accelerometer tilt for on-screen keyboard (397). Call from KeyboardEntryActivity only.
  void setKeyboardNavActive(bool active);
  void updateKeyboardNav();
  KeyboardTiltDir consumeKeyboardTilt();
  void clearKeyboardTilt();

  // Hard-shake and flip gestures (397). Poll via updateAppGestures() from main loop.
  void enableAppGestures(bool on);
  void acquireAppGestures();
  void releaseAppGestures();
  void updateAppGestures();
  // Release shared I2C for SHTC3/PMIC reads; resume afterward if gestures need the IMU.
  void suspendForSharedI2c();
  void resumeAfterSharedI2c();
  bool consumeShakeDelete();
  bool consumeShakeBookmark();
  bool consumeFlip180Return();
  void clearPendingAppGestures();
  void setShakeDeleteEnabled(bool on);
  void setFlipRelaxed(bool on);
};
