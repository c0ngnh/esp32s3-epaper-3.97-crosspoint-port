#pragma once

#include <Arduino.h>
#include <InputManager.h>

#if defined(BOARD_ESP32_S3_EPAPER_397)
// Waveshare ESP32-S3 ePaper 3.97 pin mapping
#define EPD_SCLK 11  // SPI Clock
#define EPD_MOSI 12  // SPI MOSI (Master Out Slave In)
#define EPD_CS 10    // Chip Select
#define EPD_DC 9     // Data/Command
#define EPD_RST 46   // Reset
#define EPD_BUSY 3   // Busy

// TF card uses SDIO (not display SPI). D0 is labeled MISO on the Waveshare schematic.
#define SD_MMC_CLK 16
#define SD_MMC_CMD 17
#define SD_MMC_D0 15
#define SD_MMC_D1 7
#define SD_MMC_D2 8
#define SD_MMC_D3 18

// Button pins
#define BUTTON_UP_PIN 4
#define BUTTON_FUNCTION_PIN 5
#define BUTTON_DOWN_PIN 6
// GPIO0 = BOOT (USB download). PWR key is on AXP2101 (see InputManager / HalBoard397).
#define BOARD_BOOT_BUTTON_PIN 0

// LEDs
#define ESP_LED1 43
#define ESP_LED2 44

// RTC / shared I2C bus
#define RTC_INT_PIN 45
#define I2C_SDA 41
#define I2C_SCL 42

// Audio I2S pins for ES8311 codec (Waveshare/Xiaozhi: WS=47, DIN=21; not connections.txt labels)
#define AUDIO_I2S_MCLK 13
#define AUDIO_I2S_SCLK 14
#define AUDIO_I2S_LRCK 47
#define AUDIO_I2S_DSDIN 21
#define AUDIO_I2S_DSOUT 48
#define AUDIO_CTRL_PIN 39
#define I2C_ADDR_ES8311 0x18

// Axis / IMU pins
#define AXIS_INT1_PIN 39
#define AXIS_INT2_PIN 40
#define AXIS_SDA_PIN 41
#define AXIS_SCL_PIN 42

// Power management (AXP2101)
#define AXP_PWR_IRQ_PIN 38
#define AXP_SDA 41
#define AXP_SCL 42
#define ESP_CHG_PIN 2
#define PWR_OUT_PIN 1
#define ESP_BOOT_PIN 0
#else
// Display SPI pins (custom pins for XteinkX4, not hardware SPI defaults)
#define EPD_SCLK 8   // SPI Clock
#define EPD_MOSI 10  // SPI MOSI (Master Out Slave In)
#define EPD_CS 21    // Chip Select
#define EPD_DC 4     // Data/Command
#define EPD_RST 5    // Reset
#define EPD_BUSY 6   // Busy

#define SPI_MISO 7  // SPI MISO, shared between SD card and display (Master In Slave Out)
#endif

#define BAT_GPIO0 0  // Battery voltage

#define UART0_RXD 20  // Used for USB connection detection

// Xteink X3 Hardware
#if defined(BOARD_ESP32_S3_EPAPER_397)
#define X3_I2C_SDA 41
#define X3_I2C_SCL 42
#else
#define X3_I2C_SDA 20
#define X3_I2C_SCL 0
#endif
#if defined(BOARD_ESP32_S3_EPAPER_397)
#define X3_I2C_FREQ 300000  // Waveshare reference; shared bus with SHTC3/PMIC/RTC/IMU
#else
#define X3_I2C_FREQ 400000
#endif

// TI BQ27220 Fuel gauge I2C
#define I2C_ADDR_BQ27220 0x55  // Fuel gauge I2C address
#define BQ27220_SOC_REG 0x2C   // StateOfCharge() command code (%)
#define BQ27220_CUR_REG 0x0C   // Current() command code (signed mA)
#define BQ27220_VOLT_REG 0x08  // Voltage() command code (mV)

// Analog DS3231 RTC I2C
#define I2C_ADDR_DS3231 0x68  // RTC I2C address
#define DS3231_SEC_REG 0x00   // Seconds command code (BCD)

// QST QMI8658 IMU I2C
#define I2C_ADDR_QMI8658 0x6B        // IMU I2C address
#define I2C_ADDR_QMI8658_ALT 0x6A    // IMU I2C fallback address
#define QMI8658_WHO_AM_I_REG 0x00    // WHO_AM_I command code
#define QMI8658_WHO_AM_I_VALUE 0x05  // WHO_AM_I expected value

// Sensirion SHTC3 (shared I2C bus)
#define I2C_ADDR_SHTC3 0x70

// NXP PCF85063 RTC (shared I2C bus)
#define I2C_ADDR_PCF85063 0x51

// X-Powers AXP2101 PMIC (shared I2C bus)
#define I2C_ADDR_AXP2101 0x34

class HalGPIO {
#if CROSSPOINT_EMULATED == 0
  InputManager inputMgr;
#endif

  bool lastUsbConnected = false;
  bool usbStateChanged = false;

 public:
  enum class DeviceType : uint8_t { X4, X3 };

 private:
  DeviceType _deviceType = DeviceType::X4;

 public:
  HalGPIO() = default;

  // Inline device type helpers for cleaner downstream checks
  inline bool deviceIsX3() const { return _deviceType == DeviceType::X3; }
  inline bool deviceIsX4() const { return _deviceType == DeviceType::X4; }
#if defined(BOARD_ESP32_S3_EPAPER_397)
  inline bool deviceIsEpaper397() const { return true; }
  inline bool deviceHasImu() const { return true; }
  inline bool deviceHasThreeButtonLayout() const { return true; }
#else
  inline bool deviceIsEpaper397() const { return false; }
  inline bool deviceHasImu() const { return deviceIsX3(); }
  inline bool deviceHasThreeButtonLayout() const { return false; }
#endif

  // Start button GPIO and setup SPI for screen and SD card
  void begin();

  // Button input methods
  void update();
  bool isPressed(uint8_t buttonIndex) const;
  bool wasPressed(uint8_t buttonIndex) const;
  bool wasAnyPressed() const;
  bool wasReleased(uint8_t buttonIndex) const;
  bool wasAnyReleased() const;
  unsigned long getHeldTime() const;

#if defined(BOARD_ESP32_S3_EPAPER_397)
  void clearPendingConfirmTap();
  void discardNavigationConfirm();
  void suppressConfirmAfterGesture();
  bool isConfirmSuppressed() const;
  bool consumeConfirmFullRefreshRequest();
#endif

  // Setup wake up GPIO and enter deep sleep
  void startDeepSleep();

  // Verify power button was held long enough after wakeup.
  // If verification fails, enters deep sleep and does not return.
  // Should only be called when wakeup reason is PowerButton.
  void verifyPowerButtonWakeup(uint16_t requiredDurationMs, bool shortPressAllowed);

#if defined(BOARD_ESP32_S3_EPAPER_397)
  // True when a side key (Up / Select / Down) is held after wake.
  bool isAnyUserWakeInput();
  // After GPIO wake with no button held, re-enter deep sleep without re-rendering.
  void rejectSpuriousWakeup();
  bool consumeBootSleepRequest();
  bool isBootPressed() const;
#endif

  // Check if USB is connected
  bool isUsbConnected() const;

  // Returns true once per edge (plug or unplug) since the last update()
  bool wasUsbStateChanged() const;

  enum class WakeupReason { PowerButton, AfterFlash, AfterUSBPower, Other };

  WakeupReason getWakeupReason() const;

  // Button indices
  static constexpr uint8_t BTN_BACK = 0;
  static constexpr uint8_t BTN_CONFIRM = 1;
  static constexpr uint8_t BTN_LEFT = 2;
  static constexpr uint8_t BTN_RIGHT = 3;
  static constexpr uint8_t BTN_UP = 4;
  static constexpr uint8_t BTN_DOWN = 5;
  static constexpr uint8_t BTN_POWER = 6;
};

extern HalGPIO gpio;
