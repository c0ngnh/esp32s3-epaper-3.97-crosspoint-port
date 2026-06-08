#include <HalGPIO.h>
#include <HalBoard397.h>
#include <HalDisplay.h>
#include <HalTiltSensor.h>
#include <Logging.h>
#include <Preferences.h>
#include <SPI.h>
#include <Wire.h>
#include <esp_sleep.h>
#include <driver/gpio.h>
#if defined(BOARD_ESP32_S3_EPAPER_397)
#include <HalAudio397.h>
#include <HalTimeSync397.h>
#include <driver/rtc_io.h>
#endif

// Global HalGPIO instance
HalGPIO gpio;

namespace X3GPIO {

struct X3ProbeResult {
  bool bq27220 = false;
  bool ds3231 = false;
  bool qmi8658 = false;

  uint8_t score() const {
    return static_cast<uint8_t>(bq27220) + static_cast<uint8_t>(ds3231) + static_cast<uint8_t>(qmi8658);
  }
};

bool readI2CReg8(uint8_t addr, uint8_t reg, uint8_t* outValue) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) {
    return false;
  }
  if (Wire.requestFrom(addr, static_cast<uint8_t>(1), static_cast<uint8_t>(true)) < 1) {
    return false;
  }
  *outValue = Wire.read();
  return true;
}

bool readI2CReg16LE(uint8_t addr, uint8_t reg, uint16_t* outValue) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) {
    return false;
  }
  if (Wire.requestFrom(addr, static_cast<uint8_t>(2), static_cast<uint8_t>(true)) < 2) {
    while (Wire.available()) {
      Wire.read();
    }
    return false;
  }
  const uint8_t lo = Wire.read();
  const uint8_t hi = Wire.read();
  *outValue = (static_cast<uint16_t>(hi) << 8) | lo;
  return true;
}

bool readBQ27220CurrentMA(int16_t* outCurrent) {
  uint16_t raw = 0;
  if (!readI2CReg16LE(I2C_ADDR_BQ27220, BQ27220_CUR_REG, &raw)) {
    return false;
  }
  *outCurrent = static_cast<int16_t>(raw);
  return true;
}

bool probeBQ27220Signature() {
  uint16_t soc = 0;
  uint16_t voltageMv = 0;
  if (!readI2CReg16LE(I2C_ADDR_BQ27220, BQ27220_SOC_REG, &soc)) {
    return false;
  }
  if (soc > 100) {
    return false;
  }
  if (!readI2CReg16LE(I2C_ADDR_BQ27220, BQ27220_VOLT_REG, &voltageMv)) {
    return false;
  }
  return voltageMv >= 2500 && voltageMv <= 5000;
}

bool probeDS3231Signature() {
  uint8_t sec = 0;
  if (!readI2CReg8(I2C_ADDR_DS3231, DS3231_SEC_REG, &sec)) {
    return false;
  }
  const uint8_t tensDigit = (sec >> 4) & 0x07;
  const uint8_t onesDigit = sec & 0x0F;

  return tensDigit <= 5 && onesDigit <= 9;
}

bool probeQMI8658Signature() {
  uint8_t whoami = 0;
  if (readI2CReg8(I2C_ADDR_QMI8658, QMI8658_WHO_AM_I_REG, &whoami) && whoami == QMI8658_WHO_AM_I_VALUE) {
    return true;
  }
  if (readI2CReg8(I2C_ADDR_QMI8658_ALT, QMI8658_WHO_AM_I_REG, &whoami) && whoami == QMI8658_WHO_AM_I_VALUE) {
    return true;
  }
  return false;
}

X3ProbeResult runX3ProbePass() {
  X3ProbeResult result;
  Wire.begin(X3_I2C_SDA, X3_I2C_SCL, X3_I2C_FREQ);
  Wire.setTimeOut(6);

  result.bq27220 = probeBQ27220Signature();
  result.ds3231 = probeDS3231Signature();
  result.qmi8658 = probeQMI8658Signature();

  Wire.end();
  pinMode(20, INPUT);
  pinMode(0, INPUT);
  return result;
}

}  // namespace X3GPIO

namespace {
constexpr char HW_NAMESPACE[] = "cphw";
constexpr char NVS_KEY_DEV_OVERRIDE[] = "dev_ovr";  // 0=auto, 1=x4, 2=x3
constexpr char NVS_KEY_DEV_CACHED[] = "dev_det";    // 0=unknown, 1=x4, 2=x3

enum class NvsDeviceValue : uint8_t { Unknown = 0, X4 = 1, X3 = 2 };

NvsDeviceValue readNvsDeviceValue(const char* key, NvsDeviceValue defaultValue) {
  Preferences prefs;
  if (!prefs.begin(HW_NAMESPACE, true)) {
    return defaultValue;
  }
  const uint8_t raw = prefs.getUChar(key, static_cast<uint8_t>(defaultValue));
  prefs.end();
  if (raw > static_cast<uint8_t>(NvsDeviceValue::X3)) {
    return defaultValue;
  }
  return static_cast<NvsDeviceValue>(raw);
}

void writeNvsDeviceValue(const char* key, NvsDeviceValue value) {
  Preferences prefs;
  if (!prefs.begin(HW_NAMESPACE, false)) {
    return;
  }
  prefs.putUChar(key, static_cast<uint8_t>(value));
  prefs.end();
}

HalGPIO::DeviceType nvsToDeviceType(NvsDeviceValue value) {
  return value == NvsDeviceValue::X3 ? HalGPIO::DeviceType::X3 : HalGPIO::DeviceType::X4;
}

HalGPIO::DeviceType detectDeviceTypeWithFingerprint() {
  // Explicit override for recovery/support:
  // 0 = auto, 1 = force X4, 2 = force X3
  const NvsDeviceValue overrideValue = readNvsDeviceValue(NVS_KEY_DEV_OVERRIDE, NvsDeviceValue::Unknown);
  if (overrideValue == NvsDeviceValue::X3 || overrideValue == NvsDeviceValue::X4) {
    LOG_INF("HW", "Device override active: %s", overrideValue == NvsDeviceValue::X3 ? "X3" : "X4");
    return nvsToDeviceType(overrideValue);
  }

  const NvsDeviceValue cachedValue = readNvsDeviceValue(NVS_KEY_DEV_CACHED, NvsDeviceValue::Unknown);
  if (cachedValue == NvsDeviceValue::X3 || cachedValue == NvsDeviceValue::X4) {
    LOG_INF("HW", "Using cached device type: %s", cachedValue == NvsDeviceValue::X3 ? "X3" : "X4");
    return nvsToDeviceType(cachedValue);
  }

  // No cache yet: run active X3 fingerprint probe and persist result.
  const X3GPIO::X3ProbeResult pass1 = X3GPIO::runX3ProbePass();
  delay(2);
  const X3GPIO::X3ProbeResult pass2 = X3GPIO::runX3ProbePass();

  const uint8_t score1 = pass1.score();
  const uint8_t score2 = pass2.score();
  LOG_INF("HW", "X3 probe scores: pass1=%u(bq=%d rtc=%d imu=%d) pass2=%u(bq=%d rtc=%d imu=%d)", score1, pass1.bq27220,
          pass1.ds3231, pass1.qmi8658, score2, pass2.bq27220, pass2.ds3231, pass2.qmi8658);
  const bool x3Confirmed = (score1 >= 2) && (score2 >= 2);
  const bool x4Confirmed = (score1 == 0) && (score2 == 0);

  if (x3Confirmed) {
    writeNvsDeviceValue(NVS_KEY_DEV_CACHED, NvsDeviceValue::X3);
    return HalGPIO::DeviceType::X3;
  }

  if (x4Confirmed) {
    writeNvsDeviceValue(NVS_KEY_DEV_CACHED, NvsDeviceValue::X4);
    return HalGPIO::DeviceType::X4;
  }

  // Conservative fallback for first boot with inconclusive probes.
  return HalGPIO::DeviceType::X4;
}

}  // namespace

void HalGPIO::begin() {
  inputMgr.begin();
#if !defined(BOARD_ESP32_S3_EPAPER_397)
  // Xteink: shared SPI bus for display + SD. Waveshare uses separate SPI buses — display in EInkDisplay, SD in SDCardManager.
  SPI.begin(EPD_SCLK, SPI_MISO, EPD_MOSI, EPD_CS);
#endif

#if defined(BOARD_ESP32_S3_EPAPER_397)
  HalAudio397::ensureAmplifierOff();
  _deviceType = DeviceType::X4;  // 800x480 SSD1677 panel (not X3 528x792)
  LOG_INF("HW", "Waveshare ESP32-S3 ePaper 3.97");
#else
  _deviceType = detectDeviceTypeWithFingerprint();

  if (deviceIsX4()) {
    pinMode(BAT_GPIO0, INPUT);
    pinMode(UART0_RXD, INPUT);
  }
#endif
}

void HalGPIO::update() {
  inputMgr.update();
  const bool connected = isUsbConnected();
  usbStateChanged = (connected != lastUsbConnected);
  lastUsbConnected = connected;
}

bool HalGPIO::wasUsbStateChanged() const { return usbStateChanged; }

bool HalGPIO::isPressed(uint8_t buttonIndex) const { return inputMgr.isPressed(buttonIndex); }

bool HalGPIO::wasPressed(uint8_t buttonIndex) const { return inputMgr.wasPressed(buttonIndex); }

bool HalGPIO::wasAnyPressed() const { return inputMgr.wasAnyPressed(); }

bool HalGPIO::wasReleased(uint8_t buttonIndex) const { return inputMgr.wasReleased(buttonIndex); }

bool HalGPIO::wasAnyReleased() const { return inputMgr.wasAnyReleased(); }

unsigned long HalGPIO::getHeldTime() const { return inputMgr.getHeldTime(); }

#if defined(BOARD_ESP32_S3_EPAPER_397)
void HalGPIO::clearPendingConfirmTap() { inputMgr.clearPendingConfirmTap(); }

void HalGPIO::discardNavigationConfirm() { inputMgr.discardNavigationConfirm(); }

void HalGPIO::suppressConfirmAfterGesture() { inputMgr.suppressConfirmAfterGesture(); }

bool HalGPIO::isConfirmSuppressed() const { return inputMgr.isConfirmSuppressed(); }

bool HalGPIO::consumeConfirmFullRefreshRequest() { return inputMgr.consumeConfirmFullRefreshRequest(); }

bool HalGPIO::consumeBootSleepRequest() { return inputMgr.consumeBootSleepRequest(); }

bool HalGPIO::isBootPressed() const { return inputMgr.isBootPressed(); }
#endif

void HalGPIO::startDeepSleep() {
  // Ensure buttons are released so we do not instantly wake/retrigger actions.
#if defined(BOARD_ESP32_S3_EPAPER_397)
  board397.prepareForDeepSleep();
  while (inputMgr.isPressed(BTN_CONFIRM) || inputMgr.isPressed(BTN_UP) || inputMgr.isPressed(BTN_DOWN) ||
         inputMgr.isBootPressed()) {
#else
  while (inputMgr.isPressed(BTN_POWER)) {
#endif
    delay(50);
    inputMgr.update();
  }

#if defined(BOARD_ESP32_S3_EPAPER_397)
  // GPIO0 (BOOT): cap to GND — do not use as wake. GPIO1 (PWR_OUT) does not wake reliably.
  // Side keys only: GPIO4 Up, GPIO5 Select/Function, GPIO6 Down (active LOW, internal pull-up).
  constexpr gpio_num_t kWakePins[] = {GPIO_NUM_4, GPIO_NUM_5, GPIO_NUM_6};
  for (const gpio_num_t pin : kWakePins) {
    rtc_gpio_init(pin);
    rtc_gpio_set_direction(pin, RTC_GPIO_MODE_INPUT_ONLY);
    rtc_gpio_pullup_en(pin);
    rtc_gpio_pulldown_dis(pin);
  }

  const uint64_t sideKeyMask = (1ULL << GPIO_NUM_4) | (1ULL << GPIO_NUM_5) | (1ULL << GPIO_NUM_6);
  esp_sleep_enable_ext1_wakeup(sideKeyMask, ESP_EXT1_WAKEUP_ANY_LOW);
#else
  esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_TIMER);
  esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_TOUCHPAD);
  esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ULP);
  esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_GPIO);
  esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_EXT0);
  esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_EXT1);
  gpio_wakeup_enable(static_cast<gpio_num_t>(InputManager::POWER_BUTTON_PIN), GPIO_INTR_LOW_LEVEL);
  esp_sleep_enable_gpio_wakeup();
#endif
  // Enter Deep Sleep
  esp_deep_sleep_start();
}

#if defined(BOARD_ESP32_S3_EPAPER_397)
bool HalGPIO::isAnyUserWakeInput() {
  inputMgr.update();
  delay(30);
  inputMgr.update();
  return inputMgr.isPressed(BTN_CONFIRM) || inputMgr.isPressed(BTN_UP) || inputMgr.isPressed(BTN_DOWN);
}

void HalGPIO::rejectSpuriousWakeup() {
  if (isAnyUserWakeInput()) {
    return;
  }
  LOG_DBG("MAIN", "Spurious GPIO wake (no button); returning to deep sleep");
  HalTimeSync397::cancelBeforeDeepSleep();
  halTiltSensor.releaseAppGestures();
  for (int i = 0; i < 8; ++i) {
    if (halTiltSensor.deepSleep(true)) {
      break;
    }
    delay(25);
  }
  board397.shutdownPeripheralsBeforeDeepSleep();
  display.deepSleep();
  startDeepSleep();
}
#endif

void HalGPIO::verifyPowerButtonWakeup(uint16_t requiredDurationMs, bool shortPressAllowed) {
  if (shortPressAllowed) {
    // Fast path - no duration check needed
    return;
  }
#if defined(BOARD_ESP32_S3_EPAPER_397)
  // Waveshare: accept any GPIO wake (BOOT) without requiring PWR hold time.
  (void)requiredDurationMs;
  (void)shortPressAllowed;
  return;
#endif
  constexpr uint16_t kWakeTapDebounceMs = 300;

  // Calibrate: subtract boot time already elapsed, assuming button held since boot
  const uint16_t calibration = millis();
  const uint16_t calibratedDuration = (calibration < requiredDurationMs) ? (requiredDurationMs - calibration) : 1;

  const auto start = millis();
  inputMgr.update();
  // inputMgr.isPressed() may take up to ~500ms to return correct state
  while (!inputMgr.isPressed(BTN_POWER) && millis() - start < 1000) {
    delay(10);
    inputMgr.update();
  }
  if (inputMgr.isPressed(BTN_POWER)) {
    do {
      delay(10);
      inputMgr.update();
    } while (inputMgr.isPressed(BTN_POWER) && inputMgr.getHeldTime() < calibratedDuration);
    if (inputMgr.getHeldTime() < calibratedDuration) {
      // Short press — reject wake. Debounce a trailing tap so tap-tap does not stay on.
      const unsigned long releaseMs = millis();
      while (millis() - releaseMs < kWakeTapDebounceMs) {
        delay(10);
        inputMgr.update();
        if (inputMgr.isPressed(BTN_POWER)) {
          startDeepSleep();
          return;
        }
      }
      startDeepSleep();
    }
  } else {
    startDeepSleep();
  }
}

bool HalGPIO::isUsbConnected() const {
#if defined(BOARD_ESP32_S3_EPAPER_397)
  if (board397.hasPmic()) {
    // VBUS only — the charging bit can toggle during trickle/top-off and would repaint every screen.
    return board397.isVbusPresent();
  }
  return digitalRead(ESP_CHG_PIN) == HIGH;
#else
  if (deviceIsX3()) {
    // X3: infer USB/charging via BQ27220 Current() register (0x0C, signed mA).
    // Positive current means charging.
    for (uint8_t attempt = 0; attempt < 2; ++attempt) {
      int16_t currentMa = 0;
      if (X3GPIO::readBQ27220CurrentMA(&currentMa)) {
        return currentMa > 0;
      }
      delay(2);
    }
    return false;
  }
  // U0RXD/GPIO20 reads HIGH when USB is connected
  return digitalRead(UART0_RXD) == HIGH;
#endif
}

HalGPIO::WakeupReason HalGPIO::getWakeupReason() const {
  const auto wakeupCause = esp_sleep_get_wakeup_cause();
  const auto resetReason = esp_reset_reason();

  const bool usbConnected = isUsbConnected();

#if defined(BOARD_ESP32_S3_EPAPER_397)
  if (resetReason == ESP_RST_DEEPSLEEP && wakeupCause == ESP_SLEEP_WAKEUP_EXT1) {
    return WakeupReason::PowerButton;
  }
#endif
  if ((wakeupCause == ESP_SLEEP_WAKEUP_UNDEFINED && resetReason == ESP_RST_POWERON && !usbConnected) ||
      (wakeupCause == ESP_SLEEP_WAKEUP_GPIO && resetReason == ESP_RST_DEEPSLEEP && usbConnected)) {
    return WakeupReason::PowerButton;
  }
  if (wakeupCause == ESP_SLEEP_WAKEUP_UNDEFINED && resetReason == ESP_RST_UNKNOWN && usbConnected) {
    return WakeupReason::AfterFlash;
  }
  if (wakeupCause == ESP_SLEEP_WAKEUP_UNDEFINED && resetReason == ESP_RST_POWERON && usbConnected) {
    return WakeupReason::AfterUSBPower;
  }
  return WakeupReason::Other;
}
