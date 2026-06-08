#include "HalBoard397.h"

#include <Logging.h>
#include <SDCardManager.h>
#include <Wire.h>
#include <cmath>

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include "HalGPIO.h"
#if defined(BOARD_ESP32_S3_EPAPER_397)
#include "HalTiltSensor.h"
#endif

HalBoard397 board397;

namespace {

bool wireBegun = false;
SemaphoreHandle_t i2cMutex = nullptr;

bool acquireBoardI2c(int timeoutMs);
void releaseBoardI2c();

class I2cLock {
 public:
  explicit I2cLock(int timeoutMs = 50) : held_(acquireBoardI2c(timeoutMs)) {}
  ~I2cLock() {
    if (held_) {
      releaseBoardI2c();
    }
  }
  explicit operator bool() const { return held_; }

 private:
  bool held_;
};

bool acquireBoardI2c(const int timeoutMs) {
  board397.ensureWire();
  if (i2cMutex == nullptr) {
    return true;
  }
  return xSemaphoreTake(i2cMutex, pdMS_TO_TICKS(timeoutMs)) == pdTRUE;
}

void releaseBoardI2c() {
  if (i2cMutex != nullptr) {
    xSemaphoreGive(i2cMutex);
  }
}

constexpr uint8_t AXP_REG_PMU_STATUS1 = 0x00;  // XPowers STATUS1
constexpr uint8_t AXP_REG_PMU_STATUS2 = 0x01;  // XPowers STATUS2 (charger state in bits 7:5)
constexpr uint8_t AXP_REG_ADC_CHANNEL = 0x30;
constexpr uint8_t AXP_REG_BAT_DET = 0x68;
constexpr uint8_t AXP_REG_VBUS_ILIM = 0x50;
constexpr uint8_t AXP_VBUS_ILIM_1500MA = 0x0F;  // XPowersLib: max VBUS input current
constexpr uint8_t AXP_REG_IPRECHG = 0x61;
constexpr uint8_t AXP_REG_ICC_CHG = 0x62;
constexpr uint8_t AXP_ICC_500MA = 0x0B;
constexpr uint8_t AXP_ICC_700MA = 0x0D;
constexpr uint8_t AXP_REG_ITERM = 0x63;
constexpr uint8_t AXP_REG_CV_CHG = 0x64;
constexpr uint8_t AXP_REG_CHGLED = 0x69;
constexpr uint8_t AXP_REG_INTSTS1 = 0x48;
constexpr uint8_t AXP_REG_INTSTS2 = 0x49;
constexpr uint8_t AXP_REG_INTSTS3 = 0x4A;
constexpr uint8_t AXP_REG_BATTERY_PERCENT = 0xA4;
constexpr uint8_t AXP_REG_FUEL_GAUGE_CTRL = 0xA2;
constexpr uint8_t AXP_REG_PMU_CTRL = 0x18;
constexpr uint8_t AXP_REG_BAT_VOLT_H = 0x34;   // ADC result0 H5L8
constexpr uint8_t AXP_REG_BAT_VOLT_L = 0x35;
constexpr uint8_t AXP_REG_VBUS_VOLT_H = 0x38;  // ADC result4 H6L8
constexpr uint8_t AXP_REG_VBUS_VOLT_L = 0x39;
constexpr uint8_t AXP_REG_VSYS_VOLT_H = 0x3A;  // ADC result6 H6L8
constexpr uint8_t AXP_REG_VSYS_VOLT_L = 0x3B;
constexpr uint8_t AXP_REG_DC_ONOFF = 0x80;       // DCDC1..5 enable (bit0 = DC1)
constexpr uint8_t AXP_REG_LDO_ONOFF0 = 0x90;     // ALDO1..4, BLDO1/2, CPUSLDO, DLDO1
constexpr uint8_t AXP_REG_ALDO3_VOL = 0x94;      // ALDO3 voltage (100 mV steps from 0.5 V)
constexpr uint8_t AXP_LDO_ALDO3_MASK = 0x04;     // REG90 bit2 — e-paper rail on WS397
constexpr uint8_t AXP_CHIP_ID = 0x03;
constexpr uint8_t AXP_CHIP_ID_VALUE = 0x4A;
constexpr unsigned long PMIC_LAZY_PROBE_MS = 30000;

constexpr uint8_t PCF_REG_CTRL1 = 0x00;
constexpr uint8_t PCF_REG_SECONDS = 0x04;

// SHTC3 commands (Sensirion datasheet v1.1, Table 9–14).
constexpr uint16_t SHTC3_CMD_SLEEP = 0xB098;
constexpr uint16_t SHTC3_CMD_WAKEUP = 0x3517;
constexpr uint16_t SHTC3_CMD_SOFT_RESET = 0x805D;
constexpr uint16_t SHTC3_CMD_READ_ID = 0xEFC8;
// Normal mode, temperature first, clock stretching disabled (Waveshare / Table 11).
constexpr uint16_t SHTC3_CMD_MEASURE_T_RH = 0x7866;

// Timing per SHTC3 datasheet v1.1 Table 5 (Normal mode, 25 °C).
constexpr uint32_t SHTC3_SOFT_RESET_MS = 1;   // tSR typ 180 µs; 1 ms margin
constexpr uint32_t SHTC3_MEASURE_MS = 15;     // tMEAS typ 10.8 ms, max 12.1 ms
constexpr uint32_t SHTC3_WAKEUP_MS = 50;      // Waveshare reference after wake command
constexpr uint32_t SHTC3_CMD_GAP_MS = 1;      // STOP → next START gap after command

// Waveshare PCB: subtract ~4 °C for self-heating near the e-paper stack.
constexpr float SHTC3_TEMP_OFFSET_C = 4.0f;

constexpr unsigned long BATTERY_POLL_MS = 1500;
constexpr unsigned long ENV_POLL_MS = 60000;
constexpr unsigned long ENV_REPROBE_MS = 60000;
constexpr uint8_t ENV_FAILURES_BEFORE_RECOVERY = 3;

bool shtc3WriteCmd(const uint16_t cmd, uint8_t* outErr = nullptr) {
  const uint8_t bytes[] = {static_cast<uint8_t>(cmd >> 8), static_cast<uint8_t>(cmd & 0xFF)};
  Wire.beginTransmission(I2C_ADDR_SHTC3);
  Wire.write(bytes, sizeof(bytes));
  // Datasheet §5: each command is a complete write transaction ending with STOP.
  const uint8_t err = Wire.endTransmission(true);
  if (outErr != nullptr) {
    *outErr = err;
  }
  return err == 0;
}

#if defined(BOARD_ESP32_S3_EPAPER_397)
class ImuBusPause {
 public:
  ImuBusPause() { halTiltSensor.suspendForSharedI2c(); }
  ~ImuBusPause() { halTiltSensor.resumeAfterSharedI2c(); }
};
#endif

bool shtc3ReadBytes(uint8_t* buf, const size_t len) {
  if (buf == nullptr || len == 0) {
    return false;
  }
  if (Wire.requestFrom(I2C_ADDR_SHTC3, static_cast<uint8_t>(len), static_cast<uint8_t>(true)) < len) {
    while (Wire.available()) {
      Wire.read();
    }
    return false;
  }
  for (size_t i = 0; i < len; ++i) {
    buf[i] = static_cast<uint8_t>(Wire.read());
  }
  return true;
}

bool shtc3CheckCrc(const uint8_t* data, uint8_t nbrOfBytes, uint8_t checksum) {
  uint8_t crc = 0xFF;
  for (uint8_t i = 0; i < nbrOfBytes; ++i) {
    crc ^= data[i];
    for (uint8_t bit = 8; bit > 0; --bit) {
      if (crc & 0x80) {
        crc = static_cast<uint8_t>((crc << 1) ^ 0x31);
      } else {
        crc = static_cast<uint8_t>(crc << 1);
      }
    }
  }
  return crc == checksum;
}

bool shtc3Sleep() { return shtc3WriteCmd(SHTC3_CMD_SLEEP); }

bool shtc3Wakeup() {
  if (!shtc3WriteCmd(SHTC3_CMD_WAKEUP)) {
    return false;
  }
  delay(SHTC3_WAKEUP_MS);
  return true;
}

bool shtc3SoftReset() {
  // Soft reset only works in idle state (datasheet §5.7), not sleep — wake first.
  (void)shtc3WriteCmd(SHTC3_CMD_WAKEUP);
  delay(SHTC3_CMD_GAP_MS);
  uint8_t err = 0;
  if (!shtc3WriteCmd(SHTC3_CMD_SOFT_RESET, &err)) {
    LOG_DBG("WS397", "SHTC3 soft reset NACK (Wire err %u)", err);
    return false;
  }
  delay(SHTC3_SOFT_RESET_MS + 1);
  return true;
}

float shtc3CalcTemperature(const uint16_t raw) {
  return 175.0f * (static_cast<float>(raw) / 65536.0f) - 45.0f - SHTC3_TEMP_OFFSET_C;
}

float shtc3CalcHumidity(const uint16_t raw) {
  return 100.0f * (static_cast<float>(raw) / 65536.0f);
}

bool shtc3ValuesPlausible(const HalBoard397::Environment& env) {
  return std::isfinite(env.temperatureC) && std::isfinite(env.humidityPct) && env.humidityPct >= 0.0f &&
         env.humidityPct <= 100.0f && env.temperatureC >= -40.0f && env.temperatureC <= 125.0f;
}

bool shtc3ReadId(uint16_t* outId) {
  if (outId == nullptr) {
    return false;
  }
  if (!shtc3WriteCmd(SHTC3_CMD_READ_ID)) {
    return false;
  }
  delay(SHTC3_CMD_GAP_MS);
  uint8_t buf[3] = {0};
  if (!shtc3ReadBytes(buf, sizeof(buf)) || !shtc3CheckCrc(buf, 2, buf[2])) {
    return false;
  }
  *outId = static_cast<uint16_t>((buf[0] << 8) | buf[1]);
  return true;
}

// Datasheet §5.4: wake → measure (STOP) → wait tMEAS → read T+RH → sleep.
bool shtc3Measure(HalBoard397::Environment& out) {
  if (!shtc3Wakeup()) {
    return false;
  }

  if (!shtc3WriteCmd(SHTC3_CMD_MEASURE_T_RH)) {
    (void)shtc3Sleep();
    return false;
  }

  delay(SHTC3_MEASURE_MS);

  uint8_t bytes[6] = {0};
  bool readOk = false;
  for (int attempt = 0; attempt < 3; ++attempt) {
    if (shtc3ReadBytes(bytes, sizeof(bytes))) {
      readOk = true;
      break;
    }
    delay(5);
  }
  (void)shtc3Sleep();

  if (!readOk) {
    return false;
  }

  if (!shtc3CheckCrc(bytes, 2, bytes[2]) || !shtc3CheckCrc(&bytes[3], 2, bytes[5])) {
    return false;
  }

  // 0x7866: temperature first, then humidity (datasheet Table 11).
  const uint16_t rawT = static_cast<uint16_t>((bytes[0] << 8) | bytes[1]);
  const uint16_t rawH = static_cast<uint16_t>((bytes[3] << 8) | bytes[4]);
  out.temperatureC = shtc3CalcTemperature(rawT);
  out.humidityPct = shtc3CalcHumidity(rawH);
  return shtc3ValuesPlausible(out);
}

uint8_t bcdToDec(uint8_t bcd) { return static_cast<uint8_t>(((bcd >> 4) & 0x0F) * 10 + (bcd & 0x0F)); }

uint8_t decToBcd(uint8_t dec) { return static_cast<uint8_t>(((dec / 10) << 4) | (dec % 10)); }

bool isValidBcd(uint8_t bcd, uint8_t maxOnes) {
  const uint8_t tens = (bcd >> 4) & 0x0F;
  const uint8_t ones = bcd & 0x0F;
  return tens <= (maxOnes / 10) && ones <= 9;
}

}  // namespace

bool HalBoard397::ensureWire() const {
  if (!wireBegun) {
    Wire.begin(X3_I2C_SDA, X3_I2C_SCL, X3_I2C_FREQ);
    Wire.setTimeOut(8);
    wireBegun = true;
  }
  if (i2cMutex == nullptr) {
    i2cMutex = xSemaphoreCreateMutex();
  }
  return true;
}

bool HalBoard397::readReg8(uint8_t addr, uint8_t reg, uint8_t* out) const {
  const I2cLock lock;
  if (!lock || !ensureWire()) {
    return false;
  }
  Wire.beginTransmission(addr);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) {
    return false;
  }
  if (Wire.requestFrom(addr, static_cast<uint8_t>(1), static_cast<uint8_t>(true)) < 1) {
    return false;
  }
  *out = Wire.read();
  return true;
}

bool HalBoard397::writeReg8(uint8_t addr, uint8_t reg, uint8_t val) const {
  const I2cLock lock;
  if (!lock || !ensureWire()) {
    return false;
  }
  Wire.beginTransmission(addr);
  Wire.write(reg);
  Wire.write(val);
  return Wire.endTransmission() == 0;
}

bool HalBoard397::writeRegs(uint8_t addr, const uint8_t* data, size_t len) const {
  const I2cLock lock;
  if (!lock || !ensureWire() || len == 0) {
    return false;
  }
  Wire.beginTransmission(addr);
  Wire.write(data, len);
  return Wire.endTransmission() == 0;
}

bool HalBoard397::probePmic() {
  const I2cLock lock;
  if (!lock || !ensureWire()) {
    return false;
  }
  Wire.beginTransmission(I2C_ADDR_AXP2101);
  if (Wire.endTransmission() != 0) {
    return false;
  }
  Wire.beginTransmission(I2C_ADDR_AXP2101);
  Wire.write(AXP_CHIP_ID);
  if (Wire.endTransmission(false) != 0) {
    return false;
  }
  if (Wire.requestFrom(I2C_ADDR_AXP2101, static_cast<uint8_t>(1), static_cast<uint8_t>(true)) < 1) {
    return false;
  }
  const uint8_t chipId = static_cast<uint8_t>(Wire.read());
  if (chipId == AXP_CHIP_ID_VALUE) {
    return true;
  }
  // Accept any plausible ID when the address ACKs (revision variants).
  if (chipId != 0x00 && chipId != 0xFF) {
    LOG_INF("WS397", "AXP2101 at 0x34, ID reg=0x%02x", chipId);
    return true;
  }
  return false;
}

bool HalBoard397::probeRtc() {
  uint8_t sec = 0;
  if (!readReg8(I2C_ADDR_PCF85063, PCF_REG_SECONDS, &sec)) {
    return false;
  }
  return isValidBcd(sec & 0x7F, 59);
}

bool HalBoard397::probeEnvSensor() const {
  const I2cLock lock(200);
  if (!lock || !ensureWire()) {
    return false;
  }

  if (!shtc3SoftReset()) {
    LOG_DBG("WS397", "SHTC3 soft reset failed");
    return false;
  }

  HalBoard397::Environment trial{};
  if (!shtc3Measure(trial)) {
    LOG_DBG("WS397", "SHTC3 probe measure failed");
    return false;
  }

  uint16_t id = 0;
  if (shtc3Wakeup() && shtc3ReadId(&id)) {
    LOG_INF("WS397", "SHTC3 ID 0x%04x (%.1f C %.1f %%RH)", id, trial.temperatureC, trial.humidityPct);
    (void)shtc3Sleep();
  } else {
    LOG_INF("WS397", "SHTC3 OK (%.1f C %.1f %%RH, ID read skipped)", trial.temperatureC, trial.humidityPct);
  }
  return true;
}

uint16_t axpIccCodeToMilliamps(const uint8_t code) {
  static constexpr uint16_t kTable[] = {0,   0,   0,   0,   100, 125, 150, 175, 200, 300, 400,
                                        500, 600, 700, 800, 900, 1000};
  const uint8_t idx = code & 0x1F;
  return idx < (sizeof(kTable) / sizeof(kTable[0])) ? kTable[idx] : 0;
}

bool HalBoard397::isBatteryPresent() const {
  uint8_t status1 = 0;
  if (!readReg8(I2C_ADDR_AXP2101, AXP_REG_PMU_STATUS1, &status1)) {
    return false;
  }
  return (status1 & 0x08) != 0;
}

void HalBoard397::initPmic() {
  // Match Waveshare axp_prot.cpp / XPowersLib bring-up for ESP32-S3 ePaper 3.97.
  uint8_t val = 0;
  if (readReg8(I2C_ADDR_AXP2101, AXP_REG_BAT_DET, &val)) {
    val |= 0x01;  // enableBattDetection()
    writeReg8(I2C_ADDR_AXP2101, AXP_REG_BAT_DET, val);
  }
  if (readReg8(I2C_ADDR_AXP2101, AXP_REG_ADC_CHANNEL, &val)) {
    val |= 0x1D;  // batt + vbus + vsys + temp (Waveshare axp_prot.cpp)
    writeReg8(I2C_ADDR_AXP2101, AXP_REG_ADC_CHANNEL, val);
  }
  if (readReg8(I2C_ADDR_AXP2101, AXP_REG_FUEL_GAUGE_CTRL, &val)) {
    val &= static_cast<uint8_t>(~0x10);  // fuelGaugeControl(writeROM=true, enable=true)
    val |= 0x01;
    writeReg8(I2C_ADDR_AXP2101, AXP_REG_FUEL_GAUGE_CTRL, val);
  }
  if (readReg8(I2C_ADDR_AXP2101, AXP_REG_PMU_CTRL, &val)) {
    val |= 0x0A;   // bit3 gauge module, bit1 cell charge enable
    val &= ~0x01;  // disable PMIC watchdog
    writeReg8(I2C_ADDR_AXP2101, AXP_REG_PMU_CTRL, val);
  }
  if (readReg8(I2C_ADDR_AXP2101, AXP_REG_VBUS_ILIM, &val)) {
    val = static_cast<uint8_t>((val & static_cast<uint8_t>(~0x0F)) | AXP_VBUS_ILIM_1500MA);
    writeReg8(I2C_ADDR_AXP2101, AXP_REG_VBUS_ILIM, val);
  }
  writeReg8(I2C_ADDR_AXP2101, AXP_REG_IPRECHG, 0x02);  // 50 mA precharge
  uint8_t iccCode = AXP_ICC_700MA;
  writeReg8(I2C_ADDR_AXP2101, AXP_REG_ICC_CHG, iccCode);
  uint8_t iccRead = 0;
  if (readReg8(I2C_ADDR_AXP2101, AXP_REG_ICC_CHG, &iccRead) && (iccRead & 0x1F) != iccCode) {
    iccCode = AXP_ICC_500MA;
    writeReg8(I2C_ADDR_AXP2101, AXP_REG_ICC_CHG, iccCode);
    if (readReg8(I2C_ADDR_AXP2101, AXP_REG_ICC_CHG, &iccRead)) {
      iccCode = static_cast<uint8_t>(iccRead & 0x1F);
    }
  }
  LOG_INF("WS397", "AXP charge current: %u mA (ICC reg=0x%02x)", axpIccCodeToMilliamps(iccCode), iccCode);
  writeReg8(I2C_ADDR_AXP2101, AXP_REG_ITERM, 0x01);  // 25 mA termination
  if (readReg8(I2C_ADDR_AXP2101, AXP_REG_CV_CHG, &val)) {
    val = static_cast<uint8_t>((val & static_cast<uint8_t>(~0x07)) | 0x03);  // XPOWERS_AXP2101_CHG_VOL_4V2
    writeReg8(I2C_ADDR_AXP2101, AXP_REG_CV_CHG, val);
  }
  if (readReg8(I2C_ADDR_AXP2101, AXP_REG_CHGLED, &val)) {
    val &= 0xF9;
    writeReg8(I2C_ADDR_AXP2101, AXP_REG_CHGLED, val);  // CHG LED off
  }
  // Enable PEK short/long press (AXP2101 reg 0x27).
  writeReg8(I2C_ADDR_AXP2101, 0x27, 0x07);
  // ALDO3 = 3.3 V e-paper rail (Waveshare axp_init / epaper_port EPD_Power_ON).
  writeReg8(I2C_ADDR_AXP2101, AXP_REG_ALDO3_VOL, 0x1C);  // (3300 - 500) / 100
  // Clear pending IRQ latches (write-1-to-clear per AXP2101).
  uint8_t irq = 0;
  if (readReg8(I2C_ADDR_AXP2101, AXP_REG_INTSTS1, &irq)) {
    writeReg8(I2C_ADDR_AXP2101, AXP_REG_INTSTS1, irq);
  }
  if (readReg8(I2C_ADDR_AXP2101, AXP_REG_INTSTS2, &irq)) {
    writeReg8(I2C_ADDR_AXP2101, AXP_REG_INTSTS2, irq);
  }
  if (readReg8(I2C_ADDR_AXP2101, AXP_REG_INTSTS3, &irq)) {
    writeReg8(I2C_ADDR_AXP2101, AXP_REG_INTSTS3, irq);
  }
  delay(50);  // let ADC/fuel gauge settle after channel enable
}

void HalBoard397::calibratePwrIrqIdleLevel() {
  delay(10);
  _pwrIrqIdleLevel = digitalRead(AXP_PWR_IRQ_PIN);
  _pwrIrqCalibrated = true;
}

bool HalBoard397::isPwrOutPressed() const {
  // Waveshare PMIC power-key sense: pulled up, active LOW when pressed.
  return digitalRead(PWR_OUT_PIN) == LOW;
}

void HalBoard397::recoverEnvSensorBus() const {
  const I2cLock lock(300);
  if (!lock) {
    return;
  }
  ensureWire();
#if defined(BOARD_ESP32_S3_EPAPER_397)
  const ImuBusPause imuPause;
#endif
  (void)shtc3SoftReset();
  (void)shtc3Sleep();
  LOG_INF("WS397", "SHTC3 bus recovery");
}

void HalBoard397::prepareForDeepSleep() const {
  if (!_hasPmic) {
    return;
  }
  const I2cLock lock(300);
  if (!lock) {
    return;
  }
  ensureWire();
  uint8_t irq = 0;
  if (readReg8(I2C_ADDR_AXP2101, AXP_REG_INTSTS1, &irq)) {
    writeReg8(I2C_ADDR_AXP2101, AXP_REG_INTSTS1, irq);
  }
  if (readReg8(I2C_ADDR_AXP2101, AXP_REG_INTSTS2, &irq)) {
    writeReg8(I2C_ADDR_AXP2101, AXP_REG_INTSTS2, irq);
  }
  if (readReg8(I2C_ADDR_AXP2101, AXP_REG_INTSTS3, &irq)) {
    writeReg8(I2C_ADDR_AXP2101, AXP_REG_INTSTS3, irq);
  }
}

void HalBoard397::shutdownPeripheralsBeforeDeepSleep() const {
  setLed(0, false);
  setLed(1, false);
  prepareForDeepSleep();

  const I2cLock lock(300);
  if (!lock) {
    return;
  }
  ensureWire();
#if defined(BOARD_ESP32_S3_EPAPER_397)
  const ImuBusPause imuPause;
#endif
  (void)shtc3Sleep();
}

void HalBoard397::enableEpaperRail() const {
#if !defined(BOARD_ESP32_S3_EPAPER_397)
  return;
#else
  tryLazyPmicProbe();
  if (!_hasPmic) {
    return;
  }
  const I2cLock lock(300);
  if (!lock) {
    return;
  }
  ensureWire();
  uint8_t ldo = 0;
  if (!readReg8(I2C_ADDR_AXP2101, AXP_REG_LDO_ONOFF0, &ldo)) {
    return;
  }
  if ((ldo & AXP_LDO_ALDO3_MASK) != 0) {
    return;
  }
  ldo = static_cast<uint8_t>(ldo | AXP_LDO_ALDO3_MASK);
  if (writeReg8(I2C_ADDR_AXP2101, AXP_REG_LDO_ONOFF0, ldo)) {
    LOG_DBG("WS397", "AXP ALDO3 (e-paper rail) enabled");
    delay(10);  // Waveshare EPD_Power_ON delay
  }
#endif
}

void HalBoard397::disableEpaperRail() const {
#if !defined(BOARD_ESP32_S3_EPAPER_397)
  return;
#else
  if (!_hasPmic) {
    return;
  }
  const I2cLock lock(300);
  if (!lock) {
    return;
  }
  ensureWire();
  uint8_t ldo = 0;
  if (!readReg8(I2C_ADDR_AXP2101, AXP_REG_LDO_ONOFF0, &ldo)) {
    return;
  }
  if ((ldo & AXP_LDO_ALDO3_MASK) == 0) {
    return;
  }
  ldo = static_cast<uint8_t>(ldo & static_cast<uint8_t>(~AXP_LDO_ALDO3_MASK));
  if (writeReg8(I2C_ADDR_AXP2101, AXP_REG_LDO_ONOFF0, ldo)) {
    LOG_DBG("WS397", "AXP ALDO3 (e-paper rail) disabled");
  }
#endif
}

bool HalBoard397::isPowerKeyPressed() const {
  if (!_pwrIrqCalibrated) {
    return false;
  }

  // PWR_IRQ (GPIO38) is an open-drain AXP IRQ — idle level is sampled at boot.
  // Do not treat "always LOW" as held; only report pressed when the line deviates from idle
  // for consecutive polls (debounce).
  const bool lineActive = digitalRead(AXP_PWR_IRQ_PIN) != _pwrIrqIdleLevel;
  if (lineActive) {
    if (_pwrActiveSamples < 3) {
      _pwrActiveSamples++;
    }
  } else {
    _pwrActiveSamples = 0;
  }
  return _pwrActiveSamples >= 2;
}

void HalBoard397::begin() {
#if !defined(BOARD_ESP32_S3_EPAPER_397)
  return;
#else
  pinMode(ESP_LED1, OUTPUT);
  pinMode(ESP_LED2, OUTPUT);
  digitalWrite(ESP_LED1, LOW);
  digitalWrite(ESP_LED2, LOW);

  pinMode(ESP_CHG_PIN, INPUT);
  pinMode(PWR_OUT_PIN, INPUT_PULLUP);
  pinMode(AXP_PWR_IRQ_PIN, INPUT_PULLUP);

  ensureWire();

  _hasPmic = probePmic();
  if (!_hasPmic) {
    delay(20);
    _hasPmic = probePmic();
  }
  _hasRtc = probeRtc();
  _hasEnvSensor = probeEnvSensor();
  if (!_hasEnvSensor) {
    delay(5);
    _hasEnvSensor = probeEnvSensor();
  }

  if (_hasPmic) {
    initPmic();
    delay(20);
    _batteryCached = getBatteryPercent();
    uint16_t vbatMv = 0;
    uint16_t vsysMv = 0;
    uint16_t vbusMv = 0;
    readBatteryVoltageMv(vbatMv);
    readSystemVoltageMv(vsysMv);
    readVbusVoltageMv(vbusMv);
    uint8_t st1 = 0;
    uint8_t st2 = 0;
    readReg8(I2C_ADDR_AXP2101, AXP_REG_PMU_STATUS1, &st1);
    readReg8(I2C_ADDR_AXP2101, AXP_REG_PMU_STATUS2, &st2);
    LOG_INF("WS397", "AXP init: present=%d vbat=%u vsys=%u vbus=%u soc=%u st1=0x%02x st2=0x%02x",
            isBatteryPresent() ? 1 : 0, vbatMv, vsysMv, vbusMv, _batteryCached, st1, st2);
  }

  calibratePwrIrqIdleLevel();

  _ready = _hasPmic || _hasRtc || _hasEnvSensor;

  Environment env{};
  if (_hasEnvSensor && readEnvironment(env)) {
    _envCached = env;
    _envCachedValid = true;
    _envLastPollMs = millis();
    LOG_INF("WS397", "SHTC3: %.1f C, %.1f %%RH", env.temperatureC, env.humidityPct);
  }

  DateTime rtc{};
  if (_hasRtc && readRtc(rtc)) {
    LOG_INF("WS397", "RTC: %04u-%02u-%02u %02u:%02u:%02u", rtc.year, rtc.month, rtc.day, rtc.hour, rtc.minute,
            rtc.second);
  }

  LOG_INF("WS397", "Board sensors: PMIC=%d RTC=%d SHTC3=%d batt=%u%% chg=%d vbus=%d", _hasPmic, _hasRtc,
          _hasEnvSensor, _batteryCached, isCharging() ? 1 : 0, isVbusPresent() ? 1 : 0);

  if (_ready) {
    setLed(0, true);
    delay(40);
    setLed(0, false);
  }
#endif
}

void HalBoard397::logDiagnostics(const bool imuAvailable) const {
#if defined(BOARD_ESP32_S3_EPAPER_397)
  LOG_INF("WS397", "Diagnostics: PMIC=%d RTC=%d SHTC3=%d IMU=%d SD=%d", _hasPmic ? 1 : 0, _hasRtc ? 1 : 0,
          _hasEnvSensor ? 1 : 0, imuAvailable ? 1 : 0, SDCardManager::getInstance().ready() ? 1 : 0);
  if (_hasPmic) {
    LOG_INF("WS397", "  Battery %u%% charging=%d vbus=%d", getBatteryPercent(), isCharging() ? 1 : 0,
            isVbusPresent() ? 1 : 0);
  }
  DateTime rtc{};
  if (_hasRtc && readRtc(rtc)) {
    LOG_INF("WS397", "  RTC %04u-%02u-%02u %02u:%02u:%02u", rtc.year, rtc.month, rtc.day, rtc.hour, rtc.minute,
            rtc.second);
  }
  Environment env{};
  if (_hasEnvSensor && getEnvironmentCached(env)) {
    LOG_INF("WS397", "  SHTC3 %.1f C %.1f %%RH", env.temperatureC, env.humidityPct);
  }
#endif
}

bool HalBoard397::readBatteryVoltageMv(uint16_t& millivolts) const {
  uint8_t hi = 0;
  uint8_t lo = 0;
  if (!readReg8(I2C_ADDR_AXP2101, AXP_REG_BAT_VOLT_H, &hi) || !readReg8(I2C_ADDR_AXP2101, AXP_REG_BAT_VOLT_L, &lo)) {
    return false;
  }
  millivolts = static_cast<uint16_t>(((hi & 0x1F) << 8) | lo);
  return true;
}

bool HalBoard397::readSystemVoltageMv(uint16_t& millivolts) const {
  uint8_t hi = 0;
  uint8_t lo = 0;
  if (!readReg8(I2C_ADDR_AXP2101, AXP_REG_VSYS_VOLT_H, &hi) || !readReg8(I2C_ADDR_AXP2101, AXP_REG_VSYS_VOLT_L, &lo)) {
    return false;
  }
  millivolts = static_cast<uint16_t>(((hi & 0x3F) << 8) | lo);
  return true;
}

bool HalBoard397::readVbusVoltageMv(uint16_t& millivolts) const {
  uint8_t hi = 0;
  uint8_t lo = 0;
  if (!readReg8(I2C_ADDR_AXP2101, AXP_REG_VBUS_VOLT_H, &hi) || !readReg8(I2C_ADDR_AXP2101, AXP_REG_VBUS_VOLT_L, &lo)) {
    return false;
  }
  millivolts = static_cast<uint16_t>(((hi & 0x3F) << 8) | lo);
  return true;
}

uint8_t HalBoard397::estimatePercentFromVoltageMv(const uint16_t millivolts) const {
  constexpr uint16_t kEmptyMv = 3000;
  constexpr uint16_t kFullMv = 4200;
  if (millivolts <= kEmptyMv) {
    return 0;
  }
  if (millivolts >= kFullMv) {
    return 100;
  }
  return static_cast<uint8_t>(((millivolts - kEmptyMv) * 100U) / (kFullMv - kEmptyMv));
}

void HalBoard397::tryLazyPmicProbe() const {
  if (_hasPmic) {
    return;
  }
  const unsigned long now = millis();
  if (_pmicLastLazyProbeMs != 0 && (now - _pmicLastLazyProbeMs) < PMIC_LAZY_PROBE_MS) {
    return;
  }
  _pmicLastLazyProbeMs = now;

  if (!acquireSharedI2c(50)) {
    return;
  }
  Wire.beginTransmission(I2C_ADDR_AXP2101);
  if (Wire.endTransmission() != 0) {
    releaseSharedI2c();
    return;
  }
  uint8_t chipId = 0;
  if (!readReg8(I2C_ADDR_AXP2101, AXP_CHIP_ID, &chipId)) {
    releaseSharedI2c();
    return;
  }
  if (chipId == 0x00 || chipId == 0xFF) {
    releaseSharedI2c();
    return;
  }
  _hasPmic = true;
  const_cast<HalBoard397*>(this)->initPmic();
  releaseSharedI2c();
  _batteryCached = getBatteryPercent();
  LOG_INF("WS397", "AXP2101 PMIC detected on lazy probe (batt=%u%%)", _batteryCached);
}

uint8_t HalBoard397::getBatteryPercent() const {
  tryLazyPmicProbe();
  if (!_hasPmic) {
    return 0;
  }

  const unsigned long now = millis();
  if (_batteryLastPollMs != 0 && (now - _batteryLastPollMs) < BATTERY_POLL_MS) {
    return _batteryCached;
  }

  uint8_t soc = 0;
  if (isBatteryPresent() && readReg8(I2C_ADDR_AXP2101, AXP_REG_BATTERY_PERCENT, &soc) && soc > 0) {
    _batteryCached = soc > 100 ? 100 : soc;
    _batteryLastPollMs = now;
    return _batteryCached;
  }

  uint16_t bestMv = 0;
  uint16_t mv = 0;
  for (uint8_t i = 0; i < 3; i++) {
    if (readBatteryVoltageMv(mv) && mv > bestMv) {
      bestMv = mv;
    }
    delay(5);
  }
  if (bestMv >= 2500) {
    _batteryCached = estimatePercentFromVoltageMv(bestMv);
    _batteryLastPollMs = now;
    return _batteryCached;
  }

  uint16_t vsysMv = 0;
  if (readSystemVoltageMv(vsysMv) && vsysMv >= 2500) {
    _batteryCached = estimatePercentFromVoltageMv(vsysMv);
    _batteryLastPollMs = now;
    return _batteryCached;
  }

  uint8_t status1 = 0;
  if (readReg8(I2C_ADDR_AXP2101, AXP_REG_PMU_STATUS1, &status1) && (status1 & 0x20) != 0) {
    // VBUS good but PMIC reports no pack: show full while on USB (Waveshare USB-only use case).
    _batteryCached = 100;
    _batteryLastPollMs = now;
  }
  return _batteryCached;
}

bool HalBoard397::isCharging() const {
  if (!_hasPmic) {
    return digitalRead(ESP_CHG_PIN) == HIGH;
  }
  uint8_t status = 0;
  if (!readReg8(I2C_ADDR_AXP2101, AXP_REG_PMU_STATUS2, &status)) {
    return digitalRead(ESP_CHG_PIN) == HIGH;
  }
  return ((status >> 5) & 0x07) == 0x01;
}

bool HalBoard397::isVbusPresent() const {
  if (!_hasPmic) {
    return digitalRead(ESP_CHG_PIN) == HIGH;
  }
  uint8_t status1 = 0;
  if (!readReg8(I2C_ADDR_AXP2101, AXP_REG_PMU_STATUS1, &status1)) {
    return false;
  }
  return (status1 & 0x20) != 0;  // isVbusGood() per XPowersLib
}

bool HalBoard397::readRtc(DateTime& out) const {
  if (!_hasRtc) {
    return false;
  }

  uint8_t data[7] = {};
  const I2cLock lock;
  if (!lock || !ensureWire()) {
    return false;
  }
  Wire.beginTransmission(I2C_ADDR_PCF85063);
  Wire.write(PCF_REG_SECONDS);
  if (Wire.endTransmission(false) != 0) {
    return false;
  }
  if (Wire.requestFrom(I2C_ADDR_PCF85063, static_cast<uint8_t>(7), static_cast<uint8_t>(true)) < 7) {
    return false;
  }
  for (int i = 0; i < 7; ++i) {
    data[i] = Wire.read();
  }

  out.second = bcdToDec(data[0] & 0x7F);
  out.minute = bcdToDec(data[1] & 0x7F);
  out.hour = bcdToDec(data[2] & 0x3F);
  out.day = bcdToDec(data[3] & 0x3F);
  // data[4] weekday skipped
  out.month = bcdToDec(data[5] & 0x1F);
  out.year = static_cast<uint16_t>(2000 + bcdToDec(data[6]));

  return out.month >= 1 && out.month <= 12 && out.day >= 1 && out.day <= 31;
}

bool HalBoard397::setRtc(const DateTime& dt) const {
  if (!_hasRtc) {
    return false;
  }

  const I2cLock lock;
  if (!lock) {
    return false;
  }

  const uint8_t payload[] = {
      PCF_REG_SECONDS,
      decToBcd(dt.second),
      decToBcd(dt.minute),
      decToBcd(dt.hour),
      decToBcd(dt.day),
      0,  // weekday
      decToBcd(dt.month),
      decToBcd(static_cast<uint8_t>(dt.year >= 2000 ? dt.year - 2000 : 0)),
  };
  if (!ensureWire()) {
    return false;
  }
  Wire.beginTransmission(I2C_ADDR_PCF85063);
  Wire.write(payload, sizeof(payload));
  if (Wire.endTransmission() != 0) {
    return false;
  }

  // Ensure RTC runs (clear STOP bit in control register 1).
  uint8_t ctrl = 0;
  if (readReg8(I2C_ADDR_PCF85063, PCF_REG_CTRL1, &ctrl)) {
    ctrl &= static_cast<uint8_t>(~0x20);
    writeReg8(I2C_ADDR_PCF85063, PCF_REG_CTRL1, ctrl);
  }
  return true;
}

bool HalBoard397::getEnvironmentCached(Environment& out) const {
  if (!_hasEnvSensor) {
    return false;
  }

  const unsigned long now = millis();
  if (!_envCachedValid || _envLastPollMs == 0 || (now - _envLastPollMs) >= ENV_POLL_MS) {
    // Always advance throttle time so a failed read does not hammer I2C on every call.
    _envLastPollMs = now;
    Environment fresh{};
    bool ok = false;
    for (int attempt = 0; attempt < 3; ++attempt) {
      if (readEnvironment(fresh)) {
        _envCached = fresh;
        _envCachedValid = true;
        _envConsecutiveFailures = 0;
        ok = true;
        break;
      }
      delay(5);
    }
    if (!ok) {
      _envConsecutiveFailures++;
      if (_envConsecutiveFailures >= ENV_FAILURES_BEFORE_RECOVERY) {
        recoverEnvSensorBus();
        _hasEnvSensor = probeEnvSensor();
        _envConsecutiveFailures = 0;
        if (!_hasEnvSensor) {
          _envCachedValid = false;
        }
      }
    }
  }

  if (!_envCachedValid) {
    return false;
  }
  out = _envCached;
  return true;
}

bool HalBoard397::pollEnvironment(Environment& out) const {
  const unsigned long now = millis();

  if (!_hasEnvSensor) {
    static unsigned long lastReprobeMs = 0;
    if (lastReprobeMs == 0 || (now - lastReprobeMs) >= ENV_REPROBE_MS) {
      lastReprobeMs = now;
      _hasEnvSensor = probeEnvSensor();
      if (_hasEnvSensor) {
        LOG_INF("WS397", "SHTC3 re-probed OK");
      }
    }
  }
  if (!_hasEnvSensor) {
    return false;
  }

  constexpr unsigned long kStatusBarPollMs = 5000;
  const bool needFreshRead =
      !_envCachedValid || _envLastPollMs == 0 || (now - _envLastPollMs) >= kStatusBarPollMs;
  if (needFreshRead) {
    _envLastPollMs = now;
    Environment fresh{};
    if (readEnvironment(fresh)) {
      _envCached = fresh;
      _envCachedValid = true;
      _envConsecutiveFailures = 0;
    } else {
      _envConsecutiveFailures++;
      LOG_DBG("WS397", "SHTC3 read failed (%u)", _envConsecutiveFailures);
      if (_envConsecutiveFailures >= ENV_FAILURES_BEFORE_RECOVERY) {
        recoverEnvSensorBus();
        _hasEnvSensor = probeEnvSensor();
        _envConsecutiveFailures = 0;
        if (_hasEnvSensor && readEnvironment(fresh)) {
          _envCached = fresh;
          _envCachedValid = true;
        } else if (!_hasEnvSensor) {
          _envCachedValid = false;
        }
      }
    }
  }

  return peekEnvironmentCached(out);
}

bool HalBoard397::readRtcForDisplay(DateTime& out) const {
  if (!readRtc(out)) {
    return false;
  }
  return out.year >= 2020 && out.year <= 2099 && out.month >= 1 && out.month <= 12 && out.day >= 1 &&
         out.day <= 31;
}

bool HalBoard397::readEnvironment(Environment& out) const {
  const I2cLock lock(300);
  if (!_hasEnvSensor || !lock || !ensureWire()) {
    return false;
  }
#if defined(BOARD_ESP32_S3_EPAPER_397)
  const ImuBusPause imuPause;
  (void)imuPause;
#endif
  return shtc3Measure(out);
}

bool HalBoard397::bootstrapEnvironment() {
#if !defined(BOARD_ESP32_S3_EPAPER_397)
  return false;
#else
  if (!_hasEnvSensor) {
    _hasEnvSensor = probeEnvSensor();
    if (!_hasEnvSensor) {
      LOG_INF("WS397", "SHTC3 not detected");
      return false;
    }
  }

  Environment env{};
  if (!readEnvironment(env)) {
    LOG_INF("WS397", "SHTC3 detected but read failed");
    return false;
  }

  _envCached = env;
  _envCachedValid = true;
  _envLastPollMs = millis();
  _envConsecutiveFailures = 0;
  LOG_INF("WS397", "SHTC3 ready: %.1f C, %.1f %%RH", env.temperatureC, env.humidityPct);
  return true;
#endif
}

bool HalBoard397::peekEnvironmentCached(Environment& out) const {
  if (!_hasEnvSensor || !_envCachedValid) {
    return false;
  }
  out = _envCached;
  return true;
}

bool HalBoard397::acquireSharedI2c(const int timeoutMs) const {
  return acquireBoardI2c(timeoutMs);
}

void HalBoard397::releaseSharedI2c() const { releaseBoardI2c(); }

void HalBoard397::setLed(uint8_t index, bool on) const {
#if defined(BOARD_ESP32_S3_EPAPER_397)
  const int pin = (index == 0) ? ESP_LED1 : ESP_LED2;
  digitalWrite(pin, on ? HIGH : LOW);
#else
  (void)index;
  (void)on;
#endif
}
