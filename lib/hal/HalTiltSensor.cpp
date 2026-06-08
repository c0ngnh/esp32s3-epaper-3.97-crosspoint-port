#include "HalTiltSensor.h"

#include <cmath>

#include <HalBoard397.h>
#include <Logging.h>

HalTiltSensor halTiltSensor;  // Singleton instance

#if defined(BOARD_ESP32_S3_EPAPER_397)
namespace {

struct BoardI2cLock {
  BoardI2cLock() : held_(board397.acquireSharedI2c(50)) {}
  ~BoardI2cLock() {
    if (held_) {
      board397.releaseSharedI2c();
    }
  }
  explicit operator bool() const { return held_; }

 private:
  bool held_;
};

}  // namespace
#endif

bool HalTiltSensor::writeReg(uint8_t reg, uint8_t val) const {
#if defined(BOARD_ESP32_S3_EPAPER_397)
  const BoardI2cLock lock;
  if (!lock) {
    return false;
  }
#endif
  Wire.beginTransmission(_i2cAddr);
  Wire.write(reg);
  Wire.write(val);
  return Wire.endTransmission() == 0;
}

bool HalTiltSensor::readReg(uint8_t reg, uint8_t* val) const {
#if defined(BOARD_ESP32_S3_EPAPER_397)
  const BoardI2cLock lock;
  if (!lock) {
    return false;
  }
#endif
  Wire.beginTransmission(_i2cAddr);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) {
    return false;
  }
  Wire.requestFrom(_i2cAddr, (uint8_t)1);
  if (Wire.available() < 1) {
    return false;
  }
  *val = Wire.read();
  return true;
}

bool HalTiltSensor::readAccel(float& ax, float& ay, float& az) const {
#if defined(BOARD_ESP32_S3_EPAPER_397)
  const BoardI2cLock lock;
  if (!lock) {
    return false;
  }
#endif
  Wire.beginTransmission(_i2cAddr);
  Wire.write(REG_AX_L);
  if (Wire.endTransmission(false) != 0) {
    return false;
  }

  Wire.requestFrom(_i2cAddr, static_cast<uint8_t>(6));
  if (Wire.available() < 6) {
    return false;
  }

  auto readInt16 = [&]() -> int16_t {
    const uint8_t lo = Wire.read();
    const uint8_t hi = Wire.read();
    return static_cast<int16_t>((hi << 8) | lo);
  };

  // ±4g full scale → 2048 LSB/g
  constexpr float SCALE = 1.0f / 2048.0f;
  ax = readInt16() * SCALE;
  ay = readInt16() * SCALE;
  az = readInt16() * SCALE;
  return true;
}

bool HalTiltSensor::readGyro(float& gx, float& gy, float& gz) const {
#if defined(BOARD_ESP32_S3_EPAPER_397)
  const BoardI2cLock lock;
  if (!lock) {
    return false;
  }
#endif
  Wire.beginTransmission(_i2cAddr);
  Wire.write(REG_GX_L);  // Start reading at Gyro X Low
  if (Wire.endTransmission(false) != 0) {
    return false;
  }

  Wire.requestFrom(_i2cAddr, (uint8_t)6);
  if (Wire.available() < 6) {
    return false;
  }

  auto readInt16 = [&]() -> int16_t {
    const uint8_t lo = Wire.read();
    const uint8_t hi = Wire.read();
    return static_cast<int16_t>((hi << 8) | lo);
  };

  // If Full Scale is ±512 dps, the scale factor is 32768 / 512 = 64 LSB/dps
  constexpr float SCALE = 1.0f / 64.0f;
  gx = readInt16() * SCALE;
  gy = readInt16() * SCALE;
  gz = readInt16() * SCALE;
  return true;
}

void HalTiltSensor::begin() {
  if (!gpio.deviceHasImu()) {
    _available = false;
    return;
  }

#if defined(BOARD_ESP32_S3_EPAPER_397)
  board397.ensureWire();
#endif

  // Try primary address, then alternate
  uint8_t whoami = 0;
  _i2cAddr = I2C_ADDR_QMI8658;
  if (!readReg(QMI8658_WHO_AM_I_REG, &whoami) || whoami != QMI8658_WHO_AM_I_VALUE) {
    _i2cAddr = I2C_ADDR_QMI8658_ALT;
    if (!readReg(QMI8658_WHO_AM_I_REG, &whoami) || whoami != QMI8658_WHO_AM_I_VALUE) {
      LOG_ERR("GYR", "QMI8658 IMU not found");
      _available = false;
      return;
    }
  }

  LOG_INF("GYR", "QMI8658 IMU found at 0x%02X", _i2cAddr);

  if (!writeReg(REG_CTRL7, CTRL7_DISABLE_ALL) || !writeReg(REG_CTRL3, CTRL3_FS_512DPS | CTRL3_ODR_28HZ) ||
      !writeReg(REG_CTRL1, CTRL1_BASE | CTRL1_SENSOR_DISABLE)) {
    LOG_ERR("GYR", "QMI8658 register configuration failed");
    _available = false;
    return;
  }

  _available = true;
  _initMs = millis();
  _lastPollMs = millis();
  LOG_INF("GYR", "QMI8658 gyro initialized and put to sleep");
}

bool HalTiltSensor::wakeAccelOnly() {
  if (!_available) {
    return false;
  }

#if defined(BOARD_ESP32_S3_EPAPER_397)
  board397.ensureWire();
#endif

  if ((millis() - _initMs) < SLEEP_STABILIZE_MS) {
    return false;
  }

  // Enable both accel + gyro so keyboard flick detection (gyro-based) works.
  if (writeReg(REG_CTRL1, CTRL1_BASE) && writeReg(REG_CTRL2, CTRL2_FS_4G | CTRL2_ODR_28HZ) &&
      writeReg(REG_CTRL3, CTRL3_FS_512DPS | CTRL3_ODR_28HZ) &&
      writeReg(REG_CTRL7, CTRL7_ACCEL_ENABLE | CTRL7_GYRO_ENABLE)) {
    _lastPollMs = millis();
    _keyboardWakeMs = millis();
    _wakeMs = millis();
    LOG_INF("GYR", "QMI8658 woke for keyboard flick nav");
    return true;
  }

  LOG_ERR("GYR", "Failed to wake QMI8658 for keyboard");
  return false;
}

bool HalTiltSensor::wake() {
  if (!_available) {
    return false;
  }

  // Wait for init to complete before waking
  if ((millis() - _initMs) < SLEEP_STABILIZE_MS) {
    return false;
  }

  if (writeReg(REG_CTRL1, CTRL1_BASE) && writeReg(REG_CTRL3, CTRL3_FS_512DPS | CTRL3_ODR_28HZ) &&
      writeReg(REG_CTRL7, CTRL7_GYRO_ENABLE)) {
    _lastPollMs = millis();
    _lastTiltMs = millis();
    _wakeMs = millis();
    LOG_INF("GYR", "QMI8658 woke up");
    return true;
  } else {
    LOG_ERR("GYR", "Failed to wake QMI8658");
    return false;
  }
}

bool HalTiltSensor::releaseBus() {
  if (!_available || !_isAwake) {
    return true;
  }
  if (writeReg(REG_CTRL7, CTRL7_DISABLE_ALL)) {
    clearPendingEvents();
    _inTilt = false;
    return true;
  }
  return false;
}

bool HalTiltSensor::deepSleep(const bool force) {
  if (!_available) {
    return true;
  }

  if (!_isAwake) {
    return true;
  }

  if (!force && (millis() - _wakeMs) < SLEEP_STABILIZE_MS) {
    return false;
  }

  if (releaseBus() && writeReg(REG_CTRL1, CTRL1_BASE | CTRL1_SENSOR_DISABLE)) {
    _isAwake = false;
    LOG_DBG("GYR", "QMI8658 entered sleep mode");
    return true;
  }
  LOG_ERR("GYR", "Failed to put QMI8658 to sleep");
  return false;
}

void HalTiltSensor::update(const uint8_t mode, const uint8_t orientation, const bool inReader) {
  if (!_available) {
    return;
  }

  // State machine: wake up or sleep based on the enabled flag
  if ((mode != CrossPointTiltPageTurn::TILT_OFF) && !_isAwake) {
    _isAwake = wake();
    return;
  } else if ((mode == CrossPointTiltPageTurn::TILT_OFF) && _isAwake && !_keyboardNavActive && !_appGesturesEnabled) {
    _isAwake = !deepSleep();
    return;
  }

  // If disabled, skip the rest of the polling logic and avoid unnecessary I2C traffic in non-reader activities
  if ((mode == CrossPointTiltPageTurn::TILT_OFF) || !inReader) {
    return;
  }

  const unsigned long now = millis();
  // Stabilization: discard readings during gyro startup transient
  if ((now - _wakeMs) < WAKE_STABILIZE_MS) {
    return;
  }

  if ((now - _lastPollMs) < POLL_INTERVAL_MS) {
    return;
  }
  _lastPollMs = now;

  float gx, gy, gz;
  if (!readGyro(gx, gy, gz)) {
    return;
  }

  // Map the gyro axis to left/right tilt based on reader orientation.
  // On the X3 PCB: X axis = left/right in portrait, Y axis = left/right in landscape.
  float tiltAxis;
  switch (orientation) {
    case CrossPointOrientation::PORTRAIT:
      tiltAxis = mode == CrossPointTiltPageTurn::TILT_INVERTED ? -gx : gx;
      break;
    case CrossPointOrientation::INVERTED:
      tiltAxis = mode == CrossPointTiltPageTurn::TILT_INVERTED ? gx : -gx;
      break;
    case CrossPointOrientation::LANDSCAPE_CW:
      tiltAxis = mode == CrossPointTiltPageTurn::TILT_INVERTED ? gy : -gy;
      break;
    case CrossPointOrientation::LANDSCAPE_CCW:
      tiltAxis = mode == CrossPointTiltPageTurn::TILT_INVERTED ? -gy : gy;
      break;
    default:
      tiltAxis = gx;
      break;
  }

  if (_inTilt) {
    // Wait for device to return to neutral before allowing next trigger
    if (fabsf(tiltAxis) < NEUTRAL_RATE_DPS) {
      _inTilt = false;
    }
  } else {
    // Check for new tilt gesture (with cooldown)
    if ((now - _lastTiltMs) >= COOLDOWN_MS) {
      if (tiltAxis > RATE_THRESHOLD_DPS) {
        _tiltForwardEvent = true;
        _hadActivity = true;
        _inTilt = true;
        _lastTiltMs = now;
        LOG_INF("GYR", "Forward Trigger=(%.1f) dps", tiltAxis);
      } else if (tiltAxis < -RATE_THRESHOLD_DPS) {
        _tiltBackEvent = true;
        _hadActivity = true;
        _inTilt = true;
        _lastTiltMs = now;
        LOG_INF("GYR", "Backward Trigger=(%.1f) dps", tiltAxis);
      }
    }
  }
}

bool HalTiltSensor::wasTiltedForward() {
  const bool val = _tiltForwardEvent;
  _tiltForwardEvent = false;
  return val;
}

bool HalTiltSensor::wasTiltedBack() {
  const bool val = _tiltBackEvent;
  _tiltBackEvent = false;
  return val;
}

bool HalTiltSensor::hadActivity() {
  const bool val = _hadActivity;
  _hadActivity = false;
  return val;
}

void HalTiltSensor::clearPendingEvents() {
  _tiltForwardEvent = false;
  _tiltBackEvent = false;
  _hadActivity = false;
  // Intentionally preserve _inTilt so a held tilt doesn't retrigger on next poll
}

void HalTiltSensor::setKeyboardNavActive(const bool active) {
  if (_keyboardNavActive == active) {
    return;
  }
  _keyboardNavActive = active;
  clearKeyboardTilt();

  if (!active) {
    if (_isAwake && !_appGesturesEnabled) {
      deepSleep();
      _isAwake = false;
    }
    return;
  }

  if (wakeAccelOnly()) {
    _isAwake = true;
  }
}

void HalTiltSensor::updateKeyboardNav() {
  if (!_available || !_keyboardNavActive) {
    return;
  }

#if defined(BOARD_ESP32_S3_EPAPER_397)
  board397.ensureWire();
#endif

  const unsigned long now = millis();
  if ((now - _keyboardWakeMs) < KB_WAKE_STABILIZE_MS) {
    return;
  }
  if ((now - _keyboardLastPollMs) < KB_POLL_INTERVAL_MS) {
    return;
  }
  _keyboardLastPollMs = now;

  float gx = 0.0f;
  float gy = 0.0f;
  float gz = 0.0f;
  if (!readGyro(gx, gy, gz)) {
    return;
  }

  // Portrait grip: pitch = gyro Y (rows), roll = gyro X (columns).
  const float pitchRate = gy;
  const float rollRate = gx;
  const float pitchMag = fabsf(pitchRate);
  const float rollMag = fabsf(rollRate);

  if (_keyboardInTiltPitch && pitchMag < KB_NEUTRAL_RATE_DPS) {
    _keyboardInTiltPitch = false;
  }
  if (_keyboardInTiltRoll && rollMag < KB_NEUTRAL_RATE_DPS) {
    _keyboardInTiltRoll = false;
  }
  if (_keyboardInTiltPitch || _keyboardInTiltRoll) {
    return;
  }

  if ((now - _keyboardLastTiltMs) < KB_COOLDOWN_MS) {
    return;
  }

  KeyboardTiltDir dir = KeyboardTiltDir::None;
  if (pitchMag >= KB_RATE_THRESHOLD_DPS && pitchMag >= rollMag) {
    dir = (pitchRate < 0.0f) ? KeyboardTiltDir::Down : KeyboardTiltDir::Up;
    _keyboardInTiltPitch = true;
  } else if (rollMag >= KB_RATE_THRESHOLD_DPS) {
    dir = (rollRate < 0.0f) ? KeyboardTiltDir::Left : KeyboardTiltDir::Right;
    _keyboardInTiltRoll = true;
  } else {
    return;
  }

  if (dir == _keyboardSuppressOppositeDir && now < _keyboardSuppressOppositeUntilMs) {
    return;
  }

  _keyboardTiltEvent = dir;
  _keyboardLastTiltMs = now;
  switch (dir) {
    case KeyboardTiltDir::Left:
      _keyboardSuppressOppositeDir = KeyboardTiltDir::Right;
      break;
    case KeyboardTiltDir::Right:
      _keyboardSuppressOppositeDir = KeyboardTiltDir::Left;
      break;
    case KeyboardTiltDir::Up:
      _keyboardSuppressOppositeDir = KeyboardTiltDir::Down;
      break;
    case KeyboardTiltDir::Down:
      _keyboardSuppressOppositeDir = KeyboardTiltDir::Up;
      break;
    default:
      _keyboardSuppressOppositeDir = KeyboardTiltDir::None;
      break;
  }
  _keyboardSuppressOppositeUntilMs = now + KB_OPPOSITE_SUPPRESS_MS;
  _hadActivity = true;
  LOG_DBG("GYR", "KB flick dir=%d gx=%.0f gy=%.0f gz=%.0f", static_cast<int>(dir), gx, gy, gz);
}

HalTiltSensor::KeyboardTiltDir HalTiltSensor::consumeKeyboardTilt() {
  const KeyboardTiltDir dir = _keyboardTiltEvent;
  _keyboardTiltEvent = KeyboardTiltDir::None;
  return dir;
}

void HalTiltSensor::clearKeyboardTilt() {
  _keyboardTiltEvent = KeyboardTiltDir::None;
  _keyboardInTiltPitch = false;
  _keyboardInTiltRoll = false;
  _keyboardSuppressOppositeDir = KeyboardTiltDir::None;
  _keyboardSuppressOppositeUntilMs = 0;
}

float HalTiltSensor::dominantGravityAxis(const float ax, const float ay, const float az) const {
  const float absAx = fabsf(ax);
  const float absAy = fabsf(ay);
  const float absAz = fabsf(az);
  if (absAy >= absAx && absAy >= absAz) {
    return ay;
  }
  if (absAx >= absAz) {
    return ax;
  }
  return az;
}

void HalTiltSensor::pollAppGestures(const unsigned long now) {
  float ax = 0.0f;
  float ay = 0.0f;
  float az = 0.0f;
  if (!readAccel(ax, ay, az)) {
    return;
  }

  const float magnitude = sqrtf(ax * ax + ay * ay + az * az);
  _shakeBaselineG = _shakeBaselineG * 0.92f + magnitude * 0.08f;
  const float jerk = fabsf(magnitude - _shakeBaselineG);

  const bool bookmarkShakeHit = magnitude >= SHAKE_BOOKMARK_THRESHOLD_G && jerk >= SHAKE_BOOKMARK_JERK_G;
  if (bookmarkShakeHit) {
    if (_bookmarkShakeStrikes == 0) {
      _bookmarkShakeStrikeWindowMs = now;
    }
    if ((now - _bookmarkShakeStrikeWindowMs) <= SHAKE_BOOKMARK_STRIKE_WINDOW_MS) {
      _bookmarkShakeStrikes++;
      if (_bookmarkShakeStrikes >= SHAKE_BOOKMARK_STRIKES_REQUIRED &&
          (now - _lastBookmarkShakeMs) >= SHAKE_BOOKMARK_COOLDOWN_MS) {
        _shakeBookmarkEvent = true;
        _lastBookmarkShakeMs = now;
        _bookmarkShakeStrikes = 0;
        _hadActivity = true;
        LOG_INF("GYR", "Shake bookmark prompt (%.2f g, jerk %.2f g)", magnitude, jerk);
      }
    } else {
      _bookmarkShakeStrikes = 1;
      _bookmarkShakeStrikeWindowMs = now;
    }
  } else if (_bookmarkShakeStrikes > 0 &&
             (now - _bookmarkShakeStrikeWindowMs) > SHAKE_BOOKMARK_STRIKE_WINDOW_MS) {
    _bookmarkShakeStrikes = 0;
  }

  const bool deleteShakeHit =
      _shakeDeleteEnabled && magnitude >= SHAKE_THRESHOLD_G && jerk >= SHAKE_JERK_G;
  if (deleteShakeHit) {
    if (_shakeStrikes == 0) {
      _shakeStrikeWindowMs = now;
    }
    if ((now - _shakeStrikeWindowMs) <= SHAKE_STRIKE_WINDOW_MS) {
      _shakeStrikes++;
      if (_shakeStrikes >= SHAKE_STRIKES_REQUIRED && (now - _lastShakeMs) >= SHAKE_COOLDOWN_MS) {
        _shakeDeleteEvent = true;
        _lastShakeMs = now;
        _shakeStrikes = 0;
        _hadActivity = true;
        LOG_INF("GYR", "Shake delete (%.2f g, jerk %.2f g)", magnitude, jerk);
      }
    } else {
      _shakeStrikes = 1;
      _shakeStrikeWindowMs = now;
    }
  } else if (_shakeStrikes > 0 && (now - _shakeStrikeWindowMs) > SHAKE_STRIKE_WINDOW_MS) {
    _shakeStrikes = 0;
  }

  const float g = dominantGravityAxis(ax, ay, az);
  if (!_flipRelaxed) {
    const bool stableGravity = magnitude >= FLIP_STABLE_G_MIN && magnitude <= FLIP_STABLE_G_MAX;
    if (!stableGravity) {
      if (_flipState != FlipState::Normal) {
        _flipState = FlipState::Normal;
      }
      return;
    }
  }
  switch (_flipState) {
    case FlipState::Normal:
      if (g < FLIP_INVERTED_G) {
        _flipState = FlipState::InvertedPending;
        _flipStateMs = now;
      }
      break;
    case FlipState::InvertedPending:
      if (g >= FLIP_INVERTED_G) {
        _flipState = FlipState::Normal;
      } else if ((now - _flipStateMs) >= FLIP_HOLD_MS) {
        _flipState = FlipState::Inverted;
        _flipStateMs = now;
      }
      break;
    case FlipState::Inverted:
      if (g > FLIP_NORMAL_G) {
        _flipState = FlipState::RestoringPending;
        _flipStateMs = now;
      }
      break;
    case FlipState::RestoringPending:
      if (g <= FLIP_NORMAL_G) {
        _flipState = FlipState::Inverted;
      } else if ((now - _flipStateMs) >= FLIP_HOLD_MS) {
        if ((now - _lastFlipMs) >= FLIP_COOLDOWN_MS) {
          _flip180ReturnEvent = true;
          _lastFlipMs = now;
          _hadActivity = true;
          LOG_INF("GYR", "Flip 180 return");
        }
        _flipState = FlipState::Normal;
      }
      break;
  }
}

void HalTiltSensor::suspendForSharedI2c() {
  if (!_available || !_isAwake || _suspendedForI2c) {
    return;
  }
  if (releaseBus()) {
    _suspendedForI2c = true;
    _isAwake = false;
  }
}

void HalTiltSensor::resumeAfterSharedI2c() {
  if (!_available || !_suspendedForI2c) {
    return;
  }
  _suspendedForI2c = false;
  if (_appGesturesEnabled || _keyboardNavActive) {
    if (wakeAccelOnly()) {
      _isAwake = true;
    }
  }
}

void HalTiltSensor::acquireAppGestures() {
  if (_appGestureRefCount < 255) {
    ++_appGestureRefCount;
  }
  if (_appGestureRefCount != 1) {
    return;
  }
  _appGesturesEnabled = true;
  _shakeDeleteEvent = false;
  _shakeBookmarkEvent = false;
  _shakeStrikes = 0;
  _bookmarkShakeStrikes = 0;
  _flip180ReturnEvent = false;
  _flipState = FlipState::Normal;
  if (wakeAccelOnly()) {
    _isAwake = true;
  }
}

void HalTiltSensor::releaseAppGestures() {
  if (_appGestureRefCount == 0) {
    return;
  }
  --_appGestureRefCount;
  if (_appGestureRefCount != 0) {
    return;
  }
  _appGesturesEnabled = false;
  (void)deepSleep(true);
}

void HalTiltSensor::enableAppGestures(const bool on) {
  if (on) {
    acquireAppGestures();
  } else {
    releaseAppGestures();
  }
}

void HalTiltSensor::updateAppGestures() {
  if (!_available || !_appGesturesEnabled) {
    return;
  }

#if defined(BOARD_ESP32_S3_EPAPER_397)
  board397.ensureWire();
#endif

  if (!_isAwake && !wakeAccelOnly()) {
    return;
  }
  _isAwake = true;

  const unsigned long now = millis();
  if ((now - _wakeMs) < WAKE_STABILIZE_MS) {
    return;
  }
  if ((now - _lastAppPollMs) < APP_GESTURE_POLL_MS) {
    return;
  }
  _lastAppPollMs = now;
  pollAppGestures(now);
}

bool HalTiltSensor::consumeShakeDelete() {
  const bool val = _shakeDeleteEvent;
  _shakeDeleteEvent = false;
  return val;
}

bool HalTiltSensor::consumeShakeBookmark() {
  const bool val = _shakeBookmarkEvent;
  _shakeBookmarkEvent = false;
  return val;
}

bool HalTiltSensor::consumeFlip180Return() {
  const bool val = _flip180ReturnEvent;
  _flip180ReturnEvent = false;
  return val;
}

void HalTiltSensor::clearPendingAppGestures() {
  _shakeDeleteEvent = false;
  _shakeBookmarkEvent = false;
  _shakeStrikes = 0;
  _bookmarkShakeStrikes = 0;
  _flip180ReturnEvent = false;
  _flipState = FlipState::Normal;
}

void HalTiltSensor::setShakeDeleteEnabled(const bool on) {
  _shakeDeleteEnabled = on;
  if (!on) {
    _shakeDeleteEvent = false;
    _shakeStrikes = 0;
  }
}

void HalTiltSensor::setFlipRelaxed(const bool on) {
  _flipRelaxed = on;
  if (!on) {
    _flipState = FlipState::Normal;
  }
}
