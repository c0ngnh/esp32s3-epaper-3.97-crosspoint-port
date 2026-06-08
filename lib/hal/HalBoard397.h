#pragma once

#include <Arduino.h>

// Waveshare ESP32-S3-ePaper-3.97 onboard peripherals (shared I2C on GPIO41/42).
class HalBoard397 {
 public:
  struct DateTime {
    uint16_t year = 2000;
    uint8_t month = 1;
    uint8_t day = 1;
    uint8_t hour = 0;
    uint8_t minute = 0;
    uint8_t second = 0;
  };

  struct Environment {
    float temperatureC = NAN;
    float humidityPct = NAN;
  };

  void begin();

  // Log PMIC/RTC/SHTC3 probe state; pass IMU availability from HalTiltSensor::isAvailable().
  void logDiagnostics(bool imuAvailable) const;

  // Shared I2C bus (GPIO41/42). Safe to call from IMU and other drivers after begin().
  bool ensureWire() const;

  // Serialize access to the shared I2C bus (SHTC3, RTC, PMIC, QMI8658, ES8311).
  bool acquireSharedI2c(int timeoutMs = 50) const;
  void releaseSharedI2c() const;

  bool isReady() const { return _ready; }
  bool hasPmic() const { return _hasPmic; }
  bool hasRtc() const { return _hasRtc; }
  bool hasEnvSensor() const { return _hasEnvSensor; }

  uint8_t getBatteryPercent() const;
  bool isCharging() const;
  bool isVbusPresent() const;

  bool readRtc(DateTime& out) const;
  bool setRtc(const DateTime& dt) const;

  bool readEnvironment(Environment& out) const;

  // Returns last good reading; re-polls the sensor at most every few seconds.
  bool getEnvironmentCached(Environment& out) const;

  // Re-probes SHTC3 if needed, then returns a fresh/cached reading (main loop only).
  bool pollEnvironment(Environment& out) const;

  // Last good reading without I2C (safe from render/UI thread).
  bool peekEnvironmentCached(Environment& out) const;

  // Probe/read SHTC3 while the bus is quiet (call after serial + before IMU gestures).
  bool bootstrapEnvironment();

  // RTC wall-clock for display (valid year/month/day).
  bool readRtcForDisplay(DateTime& out) const;

  void setLed(uint8_t index, bool on) const;

  // AXP2101 PEK (PWR key) via GPIO38 IRQ — debounced.
  bool isPowerKeyPressed() const;

  // GPIO1 PWR_OUT: PMIC power-key sense (INPUT_PULLUP, active LOW when pressed).
  bool isPwrOutPressed() const;

  // Clear AXP IRQ latches before esp_deep_sleep to avoid spurious GPIO38 activity.
  void prepareForDeepSleep() const;

  // Put onboard I2C sensors in low-power state before esp_deep_sleep.
  void shutdownPeripheralsBeforeDeepSleep() const;

  // ALDO3 feeds the 3.97" panel rail (Waveshare epaper_port EPD_Power_ON/OFF).
  void enableEpaperRail() const;
  void disableEpaperRail() const;

  // Idle level of PWR_IRQ (GPIO38) sampled at boot; used for wake polarity.
  int getPwrIrqIdleLevel() const { return _pwrIrqIdleLevel; }
  bool isPwrIrqCalibrated() const { return _pwrIrqCalibrated; }

 private:
  bool _ready = false;
  mutable bool _hasPmic = false;
  mutable unsigned long _pmicLastLazyProbeMs = 0;
  bool _hasRtc = false;
  mutable bool _hasEnvSensor = false;
  mutable uint8_t _batteryCached = 0;
  mutable unsigned long _batteryLastPollMs = 0;
  mutable Environment _envCached{};
  mutable bool _envCachedValid = false;
  mutable unsigned long _envLastPollMs = 0;
  mutable uint8_t _envConsecutiveFailures = 0;

  void recoverEnvSensorBus() const;
  int _pwrIrqIdleLevel = HIGH;
  bool _pwrIrqCalibrated = false;
  mutable uint8_t _pwrActiveSamples = 0;

  void calibratePwrIrqIdleLevel();
  void tryLazyPmicProbe() const;
  bool isBatteryPresent() const;
  bool readBatteryVoltageMv(uint16_t& millivolts) const;
  bool readSystemVoltageMv(uint16_t& millivolts) const;
  bool readVbusVoltageMv(uint16_t& millivolts) const;
  uint8_t estimatePercentFromVoltageMv(uint16_t millivolts) const;
  bool readReg8(uint8_t addr, uint8_t reg, uint8_t* out) const;
  bool writeReg8(uint8_t addr, uint8_t reg, uint8_t val) const;
  bool writeRegs(uint8_t addr, const uint8_t* data, size_t len) const;

  bool probePmic();
  bool probeRtc();
  bool probeEnvSensor() const;
  void initPmic();
};

extern HalBoard397 board397;
