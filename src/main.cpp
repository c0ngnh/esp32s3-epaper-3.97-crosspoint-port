#include <Arduino.h>
#include <Epub.h>
#include <FontCacheManager.h>
#include <FontDecompressor.h>
#include <GfxRenderer.h>
#include <HalDisplay.h>
#include <HalBoard397.h>
#if defined(BOARD_ESP32_S3_EPAPER_397)
#include <AlarmSound397.h>
#include <AudioFilePlayer.h>
#include <HalAudio397.h>
#include <HalTimeSync397.h>
#include <WiFi.h>
#endif
#include <HalGPIO.h>
#include <HalPowerManager.h>
#include <HalStorage.h>
#include <HalSystem.h>
#include <HalTiltSensor.h>
#include <I18n.h>
#include <Logging.h>
#include <SPI.h>
#include <builtinFonts/all.h>

#include <cstring>

#include "DeviceSleep.h"
#include "BookmarkStore.h"
#include "BootHealth.h"
#include "ClockStore.h"
#include "CrossPointSettings.h"
#include "CrossPointState.h"
#include "KOReaderCredentialStore.h"
#include "MappedInputManager.h"
#include "OpdsServerStore.h"
#include "RecentBooksStore.h"
#if defined(BOARD_ESP32_S3_EPAPER_397)
#include "SdMediaFolders.h"
#endif
#include "SdCardFontSystem.h"
#include "activities/Activity.h"
#include "activities/ActivityManager.h"
#include "activities/settings/SdFirmwareUpdateActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/ButtonNavigator.h"
#include "util/PsramAlloc.h"
#include "util/ScreenshotUtil.h"

MappedInputManager mappedInputManager(gpio);
GfxRenderer renderer(display);
ActivityManager activityManager(renderer, mappedInputManager);

namespace {
// Global screenshot: Select (Confirm) + Back. On 397, Back is BOOT while held.
bool isScreenshotBackHeld() {
#if defined(BOARD_ESP32_S3_EPAPER_397)
  return gpio.isBootPressed();
#else
  return mappedInputManager.isPressed(MappedInputManager::Button::Back);
#endif
}

bool isScreenshotSelectHeld() { return mappedInputManager.isPressed(MappedInputManager::Button::Confirm); }

#if defined(BOARD_ESP32_S3_EPAPER_397)
constexpr unsigned long SCREENSHOT_COMBO_HOLD_MS = 450;
#endif

void pollScreenshotCombo() {
  if (activityManager.isCrashReportActive()) {
    return;
  }
#if defined(BOARD_ESP32_S3_EPAPER_397)
  static unsigned long comboStartMs = 0;
  static bool comboActive = false;
  static bool firedThisCombo = false;

  const bool both = isScreenshotSelectHeld() && isScreenshotBackHeld();
  if (both) {
    gpio.clearPendingConfirmTap();
    gpio.suppressConfirmAfterGesture();
    if (!comboActive) {
      comboActive = true;
      comboStartMs = millis();
      firedThisCombo = false;
    } else if (!firedThisCombo && millis() - comboStartMs >= SCREENSHOT_COMBO_HOLD_MS) {
      firedThisCombo = true;
      gpio.suppressConfirmAfterGesture();
      RenderLock lock;
      ScreenshotUtil::takeScreenshot(renderer);
    }
  } else {
    comboActive = false;
    firedThisCombo = false;
  }
#else
  static bool screenshotButtonsReleased = true;
  if (isScreenshotSelectHeld() && isScreenshotBackHeld()) {
    if (screenshotButtonsReleased) {
      screenshotButtonsReleased = false;
      RenderLock lock;
      ScreenshotUtil::takeScreenshot(renderer);
    }
  } else {
    screenshotButtonsReleased = true;
  }
#endif
}
}  // namespace
FontDecompressor fontDecompressor;
SdCardFontSystem sdFontSystem;
FontCacheManager fontCacheManager(renderer.getFontMap(), renderer.getSdCardFonts());

// Fonts
EpdFont notoserif14RegularFont(&notoserif_14_regular);
EpdFont notoserif14BoldFont(&notoserif_14_bold);
EpdFont notoserif14ItalicFont(&notoserif_14_italic);
EpdFont notoserif14BoldItalicFont(&notoserif_14_bolditalic);
EpdFontFamily notoserif14FontFamily(&notoserif14RegularFont, &notoserif14BoldFont, &notoserif14ItalicFont,
                                    &notoserif14BoldItalicFont);
#ifndef OMIT_FONTS
EpdFont notoserif12RegularFont(&notoserif_12_regular);
EpdFont notoserif12BoldFont(&notoserif_12_bold);
EpdFont notoserif12ItalicFont(&notoserif_12_italic);
EpdFont notoserif12BoldItalicFont(&notoserif_12_bolditalic);
EpdFontFamily notoserif12FontFamily(&notoserif12RegularFont, &notoserif12BoldFont, &notoserif12ItalicFont,
                                    &notoserif12BoldItalicFont);
EpdFont notoserif16RegularFont(&notoserif_16_regular);
EpdFont notoserif16BoldFont(&notoserif_16_bold);
EpdFont notoserif16ItalicFont(&notoserif_16_italic);
EpdFont notoserif16BoldItalicFont(&notoserif_16_bolditalic);
EpdFontFamily notoserif16FontFamily(&notoserif16RegularFont, &notoserif16BoldFont, &notoserif16ItalicFont,
                                    &notoserif16BoldItalicFont);
EpdFont notoserif18RegularFont(&notoserif_18_regular);
EpdFont notoserif18BoldFont(&notoserif_18_bold);
EpdFont notoserif18ItalicFont(&notoserif_18_italic);
EpdFont notoserif18BoldItalicFont(&notoserif_18_bolditalic);
EpdFontFamily notoserif18FontFamily(&notoserif18RegularFont, &notoserif18BoldFont, &notoserif18ItalicFont,
                                    &notoserif18BoldItalicFont);

EpdFont notosans12RegularFont(&notosans_12_regular);
EpdFont notosans12BoldFont(&notosans_12_bold);
EpdFont notosans12ItalicFont(&notosans_12_italic);
EpdFont notosans12BoldItalicFont(&notosans_12_bolditalic);
EpdFontFamily notosans12FontFamily(&notosans12RegularFont, &notosans12BoldFont, &notosans12ItalicFont,
                                   &notosans12BoldItalicFont);
EpdFont notosans14RegularFont(&notosans_14_regular);
EpdFont notosans14BoldFont(&notosans_14_bold);
EpdFont notosans14ItalicFont(&notosans_14_italic);
EpdFont notosans14BoldItalicFont(&notosans_14_bolditalic);
EpdFontFamily notosans14FontFamily(&notosans14RegularFont, &notosans14BoldFont, &notosans14ItalicFont,
                                   &notosans14BoldItalicFont);
EpdFont notosans16RegularFont(&notosans_16_regular);
EpdFont notosans16BoldFont(&notosans_16_bold);
EpdFont notosans16ItalicFont(&notosans_16_italic);
EpdFont notosans16BoldItalicFont(&notosans_16_bolditalic);
EpdFontFamily notosans16FontFamily(&notosans16RegularFont, &notosans16BoldFont, &notosans16ItalicFont,
                                   &notosans16BoldItalicFont);
EpdFont notosans18RegularFont(&notosans_18_regular);
EpdFont notosans18BoldFont(&notosans_18_bold);
EpdFont notosans18ItalicFont(&notosans_18_italic);
EpdFont notosans18BoldItalicFont(&notosans_18_bolditalic);
EpdFontFamily notosans18FontFamily(&notosans18RegularFont, &notosans18BoldFont, &notosans18ItalicFont,
                                   &notosans18BoldItalicFont);

EpdFont opendyslexic8RegularFont(&opendyslexic_8_regular);
EpdFont opendyslexic8BoldFont(&opendyslexic_8_bold);
EpdFont opendyslexic8ItalicFont(&opendyslexic_8_italic);
EpdFont opendyslexic8BoldItalicFont(&opendyslexic_8_bolditalic);
EpdFontFamily opendyslexic8FontFamily(&opendyslexic8RegularFont, &opendyslexic8BoldFont, &opendyslexic8ItalicFont,
                                      &opendyslexic8BoldItalicFont);
EpdFont opendyslexic10RegularFont(&opendyslexic_10_regular);
EpdFont opendyslexic10BoldFont(&opendyslexic_10_bold);
EpdFont opendyslexic10ItalicFont(&opendyslexic_10_italic);
EpdFont opendyslexic10BoldItalicFont(&opendyslexic_10_bolditalic);
EpdFontFamily opendyslexic10FontFamily(&opendyslexic10RegularFont, &opendyslexic10BoldFont, &opendyslexic10ItalicFont,
                                       &opendyslexic10BoldItalicFont);
EpdFont opendyslexic12RegularFont(&opendyslexic_12_regular);
EpdFont opendyslexic12BoldFont(&opendyslexic_12_bold);
EpdFont opendyslexic12ItalicFont(&opendyslexic_12_italic);
EpdFont opendyslexic12BoldItalicFont(&opendyslexic_12_bolditalic);
EpdFontFamily opendyslexic12FontFamily(&opendyslexic12RegularFont, &opendyslexic12BoldFont, &opendyslexic12ItalicFont,
                                       &opendyslexic12BoldItalicFont);
EpdFont opendyslexic14RegularFont(&opendyslexic_14_regular);
EpdFont opendyslexic14BoldFont(&opendyslexic_14_bold);
EpdFont opendyslexic14ItalicFont(&opendyslexic_14_italic);
EpdFont opendyslexic14BoldItalicFont(&opendyslexic_14_bolditalic);
EpdFontFamily opendyslexic14FontFamily(&opendyslexic14RegularFont, &opendyslexic14BoldFont, &opendyslexic14ItalicFont,
                                       &opendyslexic14BoldItalicFont);
#endif  // OMIT_FONTS

EpdFont smallFont(&notosans_8_regular);
EpdFontFamily smallFontFamily(&smallFont);

EpdFont ui10RegularFont(&ubuntu_10_regular);
EpdFont ui10BoldFont(&ubuntu_10_bold);
EpdFontFamily ui10FontFamily(&ui10RegularFont, &ui10BoldFont);

EpdFont ui12RegularFont(&ubuntu_12_regular);
EpdFont ui12BoldFont(&ubuntu_12_bold);
EpdFontFamily ui12FontFamily(&ui12RegularFont, &ui12BoldFont);

// measurement of power button press duration calibration value
unsigned long t1 = 0;
unsigned long t2 = 0;

// Verify power button press duration on wake-up from deep sleep
// Pre-condition: isWakeupByPowerButton() == true
void verifyPowerButtonDuration() {
  if (SETTINGS.shortPwrBtn == CrossPointSettings::SHORT_PWRBTN::SLEEP) {
    // Fast path for short press
    // Needed because inputManager.isPressed() may take up to ~500ms to return the correct state
    return;
  }

  // Give the user up to 1000ms to start holding the power button, and must hold for SETTINGS.getPowerButtonDuration()
  const auto start = millis();
  bool abort = false;
  // Subtract the current time, because inputManager only starts counting the HeldTime from the first update()
  // This way, we remove the time we already took to reach here from the duration,
  // assuming the button was held until now from millis()==0 (i.e. device start time).
  const uint16_t calibration = start;
  const uint16_t calibratedPressDuration =
      (calibration < SETTINGS.getPowerButtonDuration()) ? SETTINGS.getPowerButtonDuration() - calibration : 1;

  gpio.update();
  // Needed because inputManager.isPressed() may take up to ~500ms to return the correct state
  while (!gpio.isPressed(HalGPIO::BTN_POWER) && millis() - start < 1000) {
    delay(10);  // only wait 10ms each iteration to not delay too much in case of short configured duration.
    gpio.update();
  }

  t2 = millis();
  if (gpio.isPressed(HalGPIO::BTN_POWER)) {
    do {
      delay(10);
      gpio.update();
    } while (gpio.isPressed(HalGPIO::BTN_POWER) && gpio.getHeldTime() < calibratedPressDuration);
    abort = gpio.getHeldTime() < calibratedPressDuration;
  } else {
    abort = true;
  }

  if (abort) {
    // Button released too early. Returning to sleep.
    // IMPORTANT: Re-arm the wakeup trigger before sleeping again
    powerManager.startDeepSleep(gpio);
  }
}
void waitForPowerRelease() {
  gpio.update();
#if defined(BOARD_ESP32_S3_EPAPER_397)
  while (gpio.isPressed(HalGPIO::BTN_CONFIRM) || gpio.isPressed(HalGPIO::BTN_UP) ||
         gpio.isPressed(HalGPIO::BTN_DOWN) || gpio.isBootPressed()) {
#else
  while (gpio.isPressed(HalGPIO::BTN_POWER)) {
#endif
    delay(50);
    gpio.update();
  }
}

namespace {

bool gDeepSleepRequested = false;
bool gDeepSleepShutdownWallpaper = false;

}  // namespace

void requestDeepSleep(const bool shutdownWallpaper) {
  gDeepSleepRequested = true;
  gDeepSleepShutdownWallpaper = shutdownWallpaper;
}

bool consumeDeepSleepRequest(bool& shutdownWallpaper) {
  if (!gDeepSleepRequested) {
    return false;
  }
  gDeepSleepRequested = false;
  shutdownWallpaper = gDeepSleepShutdownWallpaper;
  return true;
}

// Enter deep sleep mode
static void enterDeepSleep(const bool shutdownWallpaper = false) {
  HalPowerManager::Lock powerLock;  // Ensure we are at normal CPU frequency for sleep preparation
  APP_STATE.lastSleepFromReader = activityManager.isReaderActivity();
  APP_STATE.saveToFile();

  activityManager.goToSleep(shutdownWallpaper);

#if defined(BOARD_ESP32_S3_EPAPER_397)
  HalTimeSync397::cancelBeforeDeepSleep();
  audioFilePlayer.requestStop();
  if (audio397.isActive()) {
    audio397.shutdown();
  } else {
    audio397.ensureAmplifierOff();
  }
  halTiltSensor.releaseAppGestures();
  for (int i = 0; i < 8; ++i) {
    if (halTiltSensor.deepSleep(true)) {
      break;
    }
    delay(25);
  }
  board397.shutdownPeripheralsBeforeDeepSleep();
#else
  halTiltSensor.deepSleep(true);
#endif
  display.deepSleep();
#if defined(BOARD_ESP32_S3_EPAPER_397)
  // Cut ALDO3 after the panel deep-sleep command (Waveshare EPD_Power_OFF).
  board397.disableEpaperRail();
#endif
  LOG_DBG("MAIN", "Entering deep sleep");

  powerManager.startDeepSleep(gpio);
}

void setupDisplayAndFonts() {
  display.begin();
  renderer.begin();
  activityManager.begin();
  LOG_DBG("MAIN", "Display initialized");

  // Initialize font decompressor for compressed reader fonts
  if (!fontDecompressor.init()) {
    LOG_ERR("MAIN", "Font decompressor init failed");
  }
  fontCacheManager.setFontDecompressor(&fontDecompressor);
  renderer.setFontCacheManager(&fontCacheManager);
  renderer.insertFont(NOTOSERIF_14_FONT_ID, notoserif14FontFamily);
#ifndef OMIT_FONTS
  renderer.insertFont(NOTOSERIF_12_FONT_ID, notoserif12FontFamily);
  renderer.insertFont(NOTOSERIF_16_FONT_ID, notoserif16FontFamily);
  renderer.insertFont(NOTOSERIF_18_FONT_ID, notoserif18FontFamily);

  renderer.insertFont(NOTOSANS_12_FONT_ID, notosans12FontFamily);
  renderer.insertFont(NOTOSANS_14_FONT_ID, notosans14FontFamily);
  renderer.insertFont(NOTOSANS_16_FONT_ID, notosans16FontFamily);
  renderer.insertFont(NOTOSANS_18_FONT_ID, notosans18FontFamily);
  renderer.insertFont(OPENDYSLEXIC_8_FONT_ID, opendyslexic8FontFamily);
  renderer.insertFont(OPENDYSLEXIC_10_FONT_ID, opendyslexic10FontFamily);
  renderer.insertFont(OPENDYSLEXIC_12_FONT_ID, opendyslexic12FontFamily);
  renderer.insertFont(OPENDYSLEXIC_14_FONT_ID, opendyslexic14FontFamily);
#endif  // OMIT_FONTS
  renderer.insertFont(UI_10_FONT_ID, ui10FontFamily);
  renderer.insertFont(UI_12_FONT_ID, ui12FontFamily);
  renderer.insertFont(SMALL_FONT_ID, smallFontFamily);

  // Discover and load SD card fonts
  sdFontSystem.begin(renderer);

  LOG_DBG("MAIN", "Fonts setup");
}

void setup() {
  t1 = millis();

  HalSystem::begin();
  gpio.begin();

#ifdef ENABLE_SERIAL_LOG
#if defined(BOARD_ESP32_S3_EPAPER_397)
  // USB-CDC: start early so boot diagnostics are visible when the cable is connected.
  Serial.begin(115200);
  delay(80);
#else
  if (gpio.isUsbConnected()) {
    Serial.begin(115200);
    const unsigned long start = millis();
    while (!Serial && (millis() - start) < 500) {
      delay(10);
    }
  }
#endif
#endif

  powerManager.begin();
  halTiltSensor.begin();
#if defined(BOARD_ESP32_S3_EPAPER_397)
  // PMIC can miss the first probe if the shared bus is still settling; retry after IMU init.
  if (!board397.hasPmic()) {
    delay(30);
    (void)board397.getBatteryPercent();
  }
  LOG_INF("WS397", "Early boot: PMIC=%d batt=%u%%", board397.hasPmic() ? 1 : 0, board397.getBatteryPercent());
#endif

  LOG_INF("MAIN", "Hardware: %s", gpio.deviceIsEpaper397() ? "Waveshare ePaper 3.97"
                                                       : (gpio.deviceIsX3() ? "X3" : "X4"));

  // SD Card Initialization
  // We need 6 open files concurrently when parsing a new chapter
  if (!Storage.begin()) {
    LOG_ERR("MAIN", "SD card initialization failed");
    setupDisplayAndFonts();
    activityManager.goToFullScreenMessage("SD card error", EpdFontFamily::BOLD);
    return;
  }

#if defined(BOARD_ESP32_S3_EPAPER_397)
  ensureSdMediaFolders();
#endif

  HalSystem::checkPanic();

  if (!SETTINGS.loadFromFile()) {
    LOG_ERR("MAIN", "Settings load failed; using defaults (check SD /.crosspoint/settings.json)");
  }
  {
    const BootHealthReport health = runBootHealthCheck();
    LOG_INF("MAIN", "Boot health: SD=%d RTC=%d SPIRAM=%uKB settings=%s state=%s", health.sdOk, health.rtcOk,
            static_cast<unsigned>(health.spiramFree / 1024), health.settingsDetail.c_str(), health.stateDetail.c_str());
  }
  logHeapCaps("MEM");
  BOOKMARK_STORE.loadFromFile();
  CLOCK_STORE.loadFromFile();
  I18N.setLanguage(static_cast<Language>(SETTINGS.language));
  KOREADER_STORE.loadFromFile();
  OPDS_STORE.loadFromFile();
  UITheme::getInstance().reload();
  ButtonNavigator::setMappedInputManager(mappedInputManager);

  const auto wakeupReason = gpio.getWakeupReason();
  switch (wakeupReason) {
    case HalGPIO::WakeupReason::PowerButton:
#if defined(BOARD_ESP32_S3_EPAPER_397)
      LOG_DBG("MAIN", "Wake from sleep (side button)");
#else
      LOG_DBG("MAIN", "Verifying power button press duration");
      gpio.verifyPowerButtonWakeup(SETTINGS.getPowerButtonDuration(),
                                   SETTINGS.shortPwrBtn == CrossPointSettings::SHORT_PWRBTN::SLEEP);
#endif
      break;
    case HalGPIO::WakeupReason::AfterUSBPower:
      // If USB power caused a cold boot, go back to sleep
      LOG_DBG("MAIN", "Wakeup reason: After USB Power");
      powerManager.startDeepSleep(gpio);
      break;
    case HalGPIO::WakeupReason::AfterFlash:
      // After flashing, just proceed to boot
    case HalGPIO::WakeupReason::Other:
    default:
      break;
  }

  // Recovery firmware mode: hold left side button (BTN_UP) together with the power button at
  // boot to skip directly to the SD-card firmware update screen. Useful on devices where USB
  // flashing has been locked down (e.g. recent X3 firmware).
  bool recoveryFirmwareMode = false;
  if (wakeupReason == HalGPIO::WakeupReason::PowerButton) {
    // Refresh the cached button state a few times — isPressed() needs ~half a second to settle
    // after boot per the HalGPIO contract. Use a millis-based deadline so we always wait the full
    // settle window even if the loop body takes longer than expected on slow boots.
    const unsigned long settleStart = millis();
    while (millis() - settleStart < 500) {
      gpio.update();
      delay(10);
    }
    if (gpio.isPressed(HalGPIO::BTN_UP)) {
      recoveryFirmwareMode = true;
      LOG_INF("MAIN", "Recovery firmware mode (UP + POWER held at boot)");
    }
  }

  // First serial output only here to avoid timing inconsistencies for power button press duration verification
  LOG_DBG("MAIN", "Starting CrossPoint version " CROSSPOINT_VERSION);

  setupDisplayAndFonts();

#if defined(BOARD_ESP32_S3_EPAPER_397)
  if (wakeupReason == HalGPIO::WakeupReason::PowerButton) {
    gpio.rejectSpuriousWakeup();
  }
  board397.bootstrapEnvironment();
  board397.logDiagnostics(halTiltSensor.isAvailable());
#endif

  APP_STATE.loadFromFile();
  RECENT_BOOKS.loadFromFile();

#if defined(BOARD_ESP32_S3_EPAPER_397)
  const bool resumeFromDeepSleep = (wakeupReason == HalGPIO::WakeupReason::PowerButton);
#else
  const bool resumeFromDeepSleep = false;
#endif

  if (!resumeFromDeepSleep) {
    activityManager.goToBoot();
    delay(1500);
  }

  if (recoveryFirmwareMode) {
    // Skip normal home/reader routing: jump straight into the SD firmware picker.
    activityManager.replaceActivity(
        std::make_unique<SdFirmwareUpdateActivity>(renderer, mappedInputManager, /*recoveryMode=*/true));
  } else if (HalSystem::isRebootFromPanic()) {
    // If we rebooted from a panic, go to crash report screen to show the panic info
    activityManager.goToCrashReport();
  } else if (resumeFromDeepSleep) {
    if (!APP_STATE.openEpubPath.empty() && APP_STATE.lastSleepFromReader) {
      const auto path = APP_STATE.openEpubPath;
      APP_STATE.openEpubPath = "";
      APP_STATE.readerActivityLoadCount++;
      APP_STATE.saveToFile();
      activityManager.goToReader(path);
    } else {
      activityManager.goHome();
    }
  } else if (APP_STATE.openEpubPath.empty() || !APP_STATE.lastSleepFromReader ||
             mappedInputManager.isPressed(MappedInputManager::Button::Back) || APP_STATE.readerActivityLoadCount > 0) {
    // Boot to home screen if no book is open, last sleep was not from reader, back button is held, or reader activity
    // crashed (indicated by readerActivityLoadCount > 0)
    activityManager.goHome();
  } else {
    // Clear app state to avoid getting into a boot loop if the epub doesn't load
    const auto path = APP_STATE.openEpubPath;
    APP_STATE.openEpubPath = "";
    APP_STATE.readerActivityLoadCount++;
    APP_STATE.saveToFile();
    activityManager.goToReader(path);
  }

  // Ensure we're not still holding the power button before leaving setup
  waitForPowerRelease();
}

void loop() {
  static unsigned long maxLoopDuration = 0;
  const unsigned long loopStartTime = millis();
  static unsigned long lastMemPrint = 0;

  gpio.update();
#if defined(BOARD_ESP32_S3_EPAPER_397)
  if (mappedInputManager.consumeConfirmFullRefreshRequest()) {
    LOG_DBG("MAIN", "Select held 2.5s+ — full panel refresh");
    activityManager.requestUpdateAndWait();
    RenderLock lock;
    renderer.displayBuffer(HalDisplay::FULL_REFRESH);
    activityManager.requestUpdate();
  }
  if (gpio.consumeBootSleepRequest()) {
    LOG_DBG("SLP", "BOOT button held — entering deep sleep");
    enterDeepSleep(false);
    return;
  }
  {
    bool shutdownWallpaper = false;
    if (consumeDeepSleepRequest(shutdownWallpaper)) {
      LOG_DBG("SLP", "Power off from settings");
      enterDeepSleep(shutdownWallpaper);
      return;
    }
  }
  HalTimeSync397::poll();
  halTiltSensor.updateAppGestures();
  {
    int alarmIdx = -1;
    std::string alarmLabelUnused;
    if (CLOCK_STORE.pollAlarmDue(alarmIdx, alarmLabelUnused)) {
      audioFilePlayer.stopAndWait();
      const char* musicLeaf = nullptr;
      uint16_t durationSec = 60;
      if (alarmIdx >= 0 && alarmIdx < static_cast<int>(CLOCK_STORE.alarms().size())) {
        const ClockAlarm& a = CLOCK_STORE.alarms()[static_cast<size_t>(alarmIdx)];
        if (a.soundFile[0] != '\0') {
          musicLeaf = a.soundFile;
        }
        durationSec = a.soundDurationSec;
      }
      alarmPlaybackStart(musicLeaf, durationSec);
      CLOCK_STORE.markAlarmTriggered(alarmIdx);
      activityManager.goToAlarmAlert();
    }
  }
#endif
  halTiltSensor.update(SETTINGS.tiltPageTurn, SETTINGS.orientation, activityManager.isReaderActivity());

#if defined(BOARD_ESP32_S3_EPAPER_397)
  if (!activityManager.isReaderActivity()) {
    HalBoard397::Environment env{};
    board397.pollEnvironment(env);
  }
#endif

  renderer.setFadingFix(SETTINGS.fadingFix);

  if (Serial && millis() - lastMemPrint >= 10000) {
    LOG_INF("MEM", "Free: %d bytes, Total: %d bytes, Min Free: %d bytes, MaxAlloc: %d bytes", ESP.getFreeHeap(),
            ESP.getHeapSize(), ESP.getMinFreeHeap(), ESP.getMaxAllocHeap());
    logHeapCaps("MEM");
    lastMemPrint = millis();
  }

  // Handle incoming serial commands,
  // nb: we use logSerial from logging to avoid deprecation warnings
  if (logSerial.available() > 0) {
    String line = logSerial.readStringUntil('\n');
    if (line.startsWith("CMD:")) {
      String cmd = line.substring(4);
      cmd.trim();
      if (cmd == "SCREENSHOT") {
        const uint32_t bufferSize = display.getBufferSize();
        logSerial.printf("SCREENSHOT_START:%d\n", bufferSize);
        uint8_t* buf = display.getFrameBuffer();
        logSerial.write(buf, bufferSize);
        logSerial.printf("SCREENSHOT_END\n");
      }
    }
  }

  // Check for any user activity (button press or release) or active background work
  static unsigned long lastActivityTime = millis();
  if (gpio.wasAnyPressed() || gpio.wasAnyReleased() || halTiltSensor.hadActivity() ||
      activityManager.preventAutoSleep()) {
    lastActivityTime = millis();         // Reset inactivity timer
    powerManager.setPowerSaving(false);  // Restore normal CPU frequency on user activity
  }

  pollScreenshotCombo();

  const unsigned long sleepTimeoutMs = SETTINGS.getSleepTimeoutMs();
  if (millis() - lastActivityTime >= sleepTimeoutMs) {
    LOG_DBG("SLP", "Auto-sleep triggered after %lu ms of inactivity", sleepTimeoutMs);
    enterDeepSleep();
    // This should never be hit as `enterDeepSleep` calls esp_deep_sleep_start
    return;
  }

#if !defined(BOARD_ESP32_S3_EPAPER_397)
  if (gpio.isPressed(HalGPIO::BTN_POWER) && gpio.getHeldTime() > SETTINGS.getPowerButtonDuration()) {
    enterDeepSleep();
    return;
  }
#endif
  // Waveshare 397: auto-sleep via Settings timeout; BOOT held 3s+ sleeps; wake on GPIO4/5/6 only (not GPIO0/1).

  // Short PWR on Waveshare 397 is logical Back (MappedInputManager); only X4/X3 use shortPwrBtn here.
  if (!gpio.deviceIsEpaper397()) {
    if (SETTINGS.shortPwrBtn == CrossPointSettings::SHORT_PWRBTN::FORCE_REFRESH &&
        mappedInputManager.wasReleased(MappedInputManager::Button::Power)) {
      LOG_DBG("MAIN", "Manual screen refresh triggered");
      RenderLock lock;
      display.requestResync();
      renderer.displayBuffer(HalDisplay::FAST_REFRESH);
    }
  }

  // Refresh the battery icon when USB is plugged or unplugged.
  // Placed after sleep guards so we never queue a render that won't be processed.
  if (gpio.wasUsbStateChanged()) {
    activityManager.requestUpdate();
  }

  const unsigned long activityStartTime = millis();
  activityManager.loop();
  const unsigned long activityDuration = millis() - activityStartTime;

  const unsigned long loopDuration = millis() - loopStartTime;
  if (loopDuration > maxLoopDuration) {
    maxLoopDuration = loopDuration;
    if (maxLoopDuration > 50) {
      LOG_DBG("LOOP", "New max loop duration: %lu ms (activity: %lu ms)", maxLoopDuration, activityDuration);
    }
  }

  // Add delay at the end of the loop to prevent tight spinning
  // When an activity requests skip loop delay (e.g., webserver running), use yield() for faster response
  // Otherwise, use longer delay to save power
  if (activityManager.skipLoopDelay()) {
    powerManager.setPowerSaving(false);  // Make sure we're at full performance when skipLoopDelay is requested
    yield();                             // Give FreeRTOS a chance to run tasks, but return immediately
  } else {
    if (millis() - lastActivityTime >= HalPowerManager::IDLE_POWER_SAVING_MS &&
        !activityManager.shouldAvoidLowCpu()) {
      // If we've been inactive for a while, increase the delay to save power
      powerManager.setPowerSaving(true);  // Lower CPU frequency after extended inactivity
      delay(50);
    } else {
      // Short delay to prevent tight loop while still being responsive
      delay(10);
    }
  }
}
