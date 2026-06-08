#include "CrossPointSettings.h"

#include <HalStorage.h>
#include <JsonSettingsIO.h>
#include <Logging.h>
#include <Serialization.h>

#include <cstring>
#include <string>


#include "I18nKeys.h"
#include "fontIds.h"

// Initialize the static instance
CrossPointSettings CrossPointSettings::instance;

void readAndValidate(FsFile& file, uint8_t& member, const uint8_t maxValue) {
  uint8_t tempValue;
  serialization::readPod(file, tempValue);
  if (tempValue < maxValue) {
    member = tempValue;
  }
}

namespace {
#if defined(BOARD_ESP32_S3_EPAPER_397)
constexpr uint8_t SETTINGS_FILE_VERSION = 3;
constexpr uint8_t SETTINGS_FILE_VERSION_LEGACY = 1;
constexpr uint8_t SETTINGS_FILE_VERSION_PRE_THEME_ENUM = 2;
#else
constexpr uint8_t SETTINGS_FILE_VERSION = 1;
#endif
constexpr char SETTINGS_FILE_BIN[] = "/.crosspoint/settings.bin";
constexpr char SETTINGS_FILE_JSON[] = "/.crosspoint/settings.json";
constexpr char SETTINGS_FILE_JSON_BAK[] = "/.crosspoint/settings.json.bak";
constexpr char SETTINGS_FILE_BAK[] = "/.crosspoint/settings.bin.bak";
constexpr char LANG_FILE_BIN[] = "/.crosspoint/language.bin";
constexpr char LANG_FILE_BAK[] = "/.crosspoint/language.bin.bak";

// Convert legacy front button layout into explicit logical->hardware mapping.
// One-time remap from pre-2026 enum: CLASSIC=0, LYRA=1, LYRA_3_COVERS=2, ROUNDEDRAFF=3
// Current enum: LYRA=0, LYRA_3_COVERS=1, ROUNDEDRAFF=2 — do not run on live settings (collides with 1/2).
void migrateLegacyUiThemeValue(uint8_t& uiTheme) {
  switch (uiTheme) {
    case 0:  // Classic or Lyra
    case 1:  // legacy Lyra
      uiTheme = CrossPointSettings::LYRA;
      break;
    case 2:  // legacy Lyra 3 Covers
      uiTheme = CrossPointSettings::LYRA_3_COVERS;
      break;
    case 3:  // legacy RoundedRaff
      uiTheme = CrossPointSettings::ROUNDEDRAFF;
      break;
    default:
      if (uiTheme >= CrossPointSettings::UI_THEME_COUNT) {
        uiTheme = CrossPointSettings::LYRA;
      }
      break;
  }
}

void applyLegacyFrontButtonLayout(CrossPointSettings& settings) {
  switch (static_cast<CrossPointSettings::FRONT_BUTTON_LAYOUT>(settings.frontButtonLayout)) {
    case CrossPointSettings::LEFT_RIGHT_BACK_CONFIRM:
      settings.frontButtonBack = CrossPointSettings::FRONT_HW_LEFT;
      settings.frontButtonConfirm = CrossPointSettings::FRONT_HW_RIGHT;
      settings.frontButtonLeft = CrossPointSettings::FRONT_HW_BACK;
      settings.frontButtonRight = CrossPointSettings::FRONT_HW_CONFIRM;
      break;
    case CrossPointSettings::LEFT_BACK_CONFIRM_RIGHT:
      settings.frontButtonBack = CrossPointSettings::FRONT_HW_CONFIRM;
      settings.frontButtonConfirm = CrossPointSettings::FRONT_HW_LEFT;
      settings.frontButtonLeft = CrossPointSettings::FRONT_HW_BACK;
      settings.frontButtonRight = CrossPointSettings::FRONT_HW_RIGHT;
      break;
    case CrossPointSettings::BACK_CONFIRM_RIGHT_LEFT:
      settings.frontButtonBack = CrossPointSettings::FRONT_HW_BACK;
      settings.frontButtonConfirm = CrossPointSettings::FRONT_HW_CONFIRM;
      settings.frontButtonLeft = CrossPointSettings::FRONT_HW_RIGHT;
      settings.frontButtonRight = CrossPointSettings::FRONT_HW_LEFT;
      break;
    case CrossPointSettings::BACK_CONFIRM_LEFT_RIGHT:
    default:
      settings.frontButtonBack = CrossPointSettings::FRONT_HW_BACK;
      settings.frontButtonConfirm = CrossPointSettings::FRONT_HW_CONFIRM;
      settings.frontButtonLeft = CrossPointSettings::FRONT_HW_LEFT;
      settings.frontButtonRight = CrossPointSettings::FRONT_HW_RIGHT;
      break;
  }
}

}  // namespace

void CrossPointSettings::migrateUiTheme(uint8_t& uiTheme) { migrateLegacyUiThemeValue(uiTheme); }

void CrossPointSettings::validateFrontButtonMapping(CrossPointSettings& settings) {
  const uint8_t mapping[] = {settings.frontButtonBack, settings.frontButtonConfirm, settings.frontButtonLeft,
                             settings.frontButtonRight};
  for (size_t i = 0; i < 4; i++) {
    for (size_t j = i + 1; j < 4; j++) {
      if (mapping[i] == mapping[j]) {
        settings.frontButtonBack = FRONT_HW_BACK;
        settings.frontButtonConfirm = FRONT_HW_CONFIRM;
        settings.frontButtonLeft = FRONT_HW_LEFT;
        settings.frontButtonRight = FRONT_HW_RIGHT;
        return;
      }
    }
  }
}

bool CrossPointSettings::saveToFile() const {
  Storage.mkdir("/.crosspoint");
  return JsonSettingsIO::saveSettings(*this, SETTINGS_FILE_JSON);
}

bool CrossPointSettings::loadFromFile() {
  // Try JSON first
  if (Storage.exists(SETTINGS_FILE_JSON)) {
    String json = Storage.readFile(SETTINGS_FILE_JSON);
    if (!json.isEmpty()) {
      bool resave = false;
      if (JsonSettingsIO::loadSettings(*this, json.c_str(), &resave)) {
        if (resave) {
          if (saveToFile()) {
            LOG_DBG("CPS", "Resaved settings to update format");
          } else {
            LOG_ERR("CPS", "Failed to resave settings after format update");
          }
        }
        migrateLanguageBinaryFile();
        return true;
      }
      LOG_ERR("CPS", "Failed to parse settings.json, trying .bak then binary");
    }
  }

  if (Storage.exists(SETTINGS_FILE_JSON_BAK)) {
    const String jsonBak = Storage.readFile(SETTINGS_FILE_JSON_BAK);
    if (!jsonBak.isEmpty()) {
      bool resave = false;
      if (JsonSettingsIO::loadSettings(*this, jsonBak.c_str(), &resave)) {
        LOG_DBG("CPS", "Loaded settings from settings.json.bak");
        if (resave) {
          saveToFile();
        }
        migrateLanguageBinaryFile();
        return true;
      }
      LOG_ERR("CPS", "Failed to parse settings.json.bak");
    }
  }

  // Fall back to binary migration
  if (Storage.exists(SETTINGS_FILE_BIN)) {
    if (loadFromBinaryFile()) {
      migrateLanguageBinaryFile();
      if (saveToFile()) {
        Storage.rename(SETTINGS_FILE_BIN, SETTINGS_FILE_BAK);
        LOG_DBG("CPS", "Migrated settings.bin to settings.json");
        return true;
      } else {
        LOG_ERR("CPS", "Failed to save migrated settings to JSON");
        return false;
      }
    }
  }

  // No settings files at all -- check for standalone language.bin
  return migrateLanguageBinaryFile();
}

bool CrossPointSettings::migrateLanguageBinaryFile() {
  // V1_LANGUAGES / V1_LANGUAGE_COUNT are emitted by gen_i18n.py with the
  // frozen enum order from 2f969a9.
  if (!Storage.exists(LANG_FILE_BIN)) return false;

  bool migrated = false;
  FsFile f;
  if (Storage.openFileForRead("CPS", LANG_FILE_BIN, f)) {
    uint8_t version = 0;
    uint8_t oldIndex = 0;
    serialization::readPod(f, version);
    serialization::readPod(f, oldIndex);
    if (version == 1 && oldIndex < V1_LANGUAGE_COUNT) {
      language = static_cast<uint8_t>(V1_LANGUAGES[oldIndex]);
      migrated = true;
    }
    f.close();
  }
  if (!migrated) {
    LOG_ERR("CPS", "language.bin unreadable, leaving file in place");
    return false;
  }
  Storage.rename(LANG_FILE_BIN, LANG_FILE_BAK);
  saveToFile();
  LOG_DBG("CPS", "Migrated language.bin into settings.json");
  return true;
}

bool CrossPointSettings::loadFromBinaryFile() {
  FsFile inputFile;
  if (!Storage.openFileForRead("CPS", SETTINGS_FILE_BIN, inputFile)) {
    return false;
  }

  uint8_t version;
  serialization::readPod(inputFile, version);
#if defined(BOARD_ESP32_S3_EPAPER_397)
  const bool legacyRefreshFrequency = (version == SETTINGS_FILE_VERSION_LEGACY);
  const bool legacyUiThemeEnum = (version == SETTINGS_FILE_VERSION_PRE_THEME_ENUM);
  if (version != SETTINGS_FILE_VERSION && !legacyRefreshFrequency && !legacyUiThemeEnum) {
    LOG_ERR("CPS", "Deserialization failed: Unknown version %u", version);
    return false;
  }
#else
  if (version != SETTINGS_FILE_VERSION) {
    LOG_ERR("CPS", "Deserialization failed: Unknown version %u", version);
    return false;
  }
#endif

  uint8_t fileSettingsCount = 0;
  serialization::readPod(inputFile, fileSettingsCount);

  uint8_t settingsRead = 0;
  bool frontButtonMappingRead = false;
  do {
    readAndValidate(inputFile, sleepScreen, SLEEP_SCREEN_MODE_COUNT);
    if (++settingsRead >= fileSettingsCount) break;
    serialization::readPod(inputFile, extraParagraphSpacing);
    if (++settingsRead >= fileSettingsCount) break;
    readAndValidate(inputFile, shortPwrBtn, SHORT_PWRBTN_COUNT);
    if (++settingsRead >= fileSettingsCount) break;
    readAndValidate(inputFile, statusBar, STATUS_BAR_MODE_COUNT);  // legacy
    if (++settingsRead >= fileSettingsCount) break;
    readAndValidate(inputFile, orientation, ORIENTATION_COUNT);
    if (++settingsRead >= fileSettingsCount) break;
    readAndValidate(inputFile, frontButtonLayout, FRONT_BUTTON_LAYOUT_COUNT);
    if (++settingsRead >= fileSettingsCount) break;
    readAndValidate(inputFile, sideButtonLayout, SIDE_BUTTON_LAYOUT_COUNT);
    if (++settingsRead >= fileSettingsCount) break;
    readAndValidate(inputFile, fontFamily, FONT_FAMILY_COUNT);
    if (++settingsRead >= fileSettingsCount) break;
    readAndValidate(inputFile, fontSize, FONT_SIZE_COUNT);
    if (++settingsRead >= fileSettingsCount) break;
    readAndValidate(inputFile, lineSpacing, LINE_COMPRESSION_COUNT);
    if (++settingsRead >= fileSettingsCount) break;
    readAndValidate(inputFile, paragraphAlignment, PARAGRAPH_ALIGNMENT_COUNT);
    if (++settingsRead >= fileSettingsCount) break;
    readAndValidate(inputFile, sleepTimeout, SLEEP_TIMEOUT_COUNT);
    if (++settingsRead >= fileSettingsCount) break;
#if defined(BOARD_ESP32_S3_EPAPER_397)
    {
      uint8_t stored = 0;
      serialization::readPod(inputFile, stored);
      if (legacyRefreshFrequency) {
        // v1 indices 0..4 were 1/5/10/15/30 pages — default tier is now 30 pages.
        (void)stored;
        refreshFrequency = REFRESH_30;
      } else if (stored <= REFRESH_60) {
        refreshFrequency = stored;
      } else {
        refreshFrequency = REFRESH_30;
      }
    }
#else
    readAndValidate(inputFile, refreshFrequency, REFRESH_FREQUENCY_COUNT);
#endif
    if (++settingsRead >= fileSettingsCount) break;
    serialization::readPod(inputFile, screenMargin);
    if (++settingsRead >= fileSettingsCount) break;
    readAndValidate(inputFile, sleepScreenCoverMode, SLEEP_SCREEN_COVER_MODE_COUNT);
    if (++settingsRead >= fileSettingsCount) break;
    {
      std::string urlStr;
      serialization::readString(inputFile, urlStr);
      strncpy(opdsServerUrl, urlStr.c_str(), sizeof(opdsServerUrl) - 1);
      opdsServerUrl[sizeof(opdsServerUrl) - 1] = '\0';
    }
    if (++settingsRead >= fileSettingsCount) break;
    serialization::readPod(inputFile, textAntiAliasing);
    if (++settingsRead >= fileSettingsCount) break;
    readAndValidate(inputFile, hideBatteryPercentage, HIDE_BATTERY_PERCENTAGE_COUNT);
    if (++settingsRead >= fileSettingsCount) break;
    readAndValidate(inputFile, longPressButtonBehavior, LONG_PRESS_BUTTON_BEHAVIOR_COUNT);
    if (++settingsRead >= fileSettingsCount) break;
    serialization::readPod(inputFile, hyphenationEnabled);
    if (++settingsRead >= fileSettingsCount) break;
    {
      std::string usernameStr;
      serialization::readString(inputFile, usernameStr);
      strncpy(opdsUsername, usernameStr.c_str(), sizeof(opdsUsername) - 1);
      opdsUsername[sizeof(opdsUsername) - 1] = '\0';
    }
    if (++settingsRead >= fileSettingsCount) break;
    {
      std::string passwordStr;
      serialization::readString(inputFile, passwordStr);
      strncpy(opdsPassword, passwordStr.c_str(), sizeof(opdsPassword) - 1);
      opdsPassword[sizeof(opdsPassword) - 1] = '\0';
    }
    if (++settingsRead >= fileSettingsCount) break;
    readAndValidate(inputFile, sleepScreenCoverFilter, SLEEP_SCREEN_COVER_FILTER_COUNT);
    if (++settingsRead >= fileSettingsCount) break;
    serialization::readPod(inputFile, uiTheme);
    if (legacyUiThemeEnum || legacyRefreshFrequency) {
      migrateLegacyUiThemeValue(uiTheme);
    } else if (uiTheme >= UI_THEME_COUNT) {
      uiTheme = LYRA;
    }
    if (++settingsRead >= fileSettingsCount) break;
    readAndValidate(inputFile, frontButtonBack, FRONT_BUTTON_HARDWARE_COUNT);
    if (++settingsRead >= fileSettingsCount) break;
    readAndValidate(inputFile, frontButtonConfirm, FRONT_BUTTON_HARDWARE_COUNT);
    if (++settingsRead >= fileSettingsCount) break;
    readAndValidate(inputFile, frontButtonLeft, FRONT_BUTTON_HARDWARE_COUNT);
    if (++settingsRead >= fileSettingsCount) break;
    readAndValidate(inputFile, frontButtonRight, FRONT_BUTTON_HARDWARE_COUNT);
    frontButtonMappingRead = true;
    if (++settingsRead >= fileSettingsCount) break;
    serialization::readPod(inputFile, fadingFix);
    if (++settingsRead >= fileSettingsCount) break;
    serialization::readPod(inputFile, embeddedStyle);
    if (++settingsRead >= fileSettingsCount) break;
  } while (false);

  if (frontButtonMappingRead) {
    CrossPointSettings::validateFrontButtonMapping(*this);
  } else {
    applyLegacyFrontButtonLayout(*this);
  }

  LOG_DBG("CPS", "Settings loaded from binary file");
  return true;
}

float CrossPointSettings::getReaderLineCompression() const {
  // SD card fonts use same compression as Bookerly (the most neutral values)
  if (sdFontFamilyName[0] != '\0') {
    switch (lineSpacing) {
      case TIGHT:
        return 0.95f;
      case NORMAL:
      default:
        return 1.0f;
      case WIDE:
        return 1.1f;
    }
  }

  switch (fontFamily) {
    case NOTOSERIF:
    default:
      switch (lineSpacing) {
        case TIGHT:
          return 0.95f;
        case NORMAL:
        default:
          return 1.0f;
        case WIDE:
          return 1.1f;
      }
    case NOTOSANS:
      switch (lineSpacing) {
        case TIGHT:
          return 0.90f;
        case NORMAL:
        default:
          return 0.95f;
        case WIDE:
          return 1.0f;
      }
    case OPENDYSLEXIC:
      switch (lineSpacing) {
        case TIGHT:
          return 0.90f;
        case NORMAL:
        default:
          return 0.95f;
        case WIDE:
          return 1.0f;
      }
  }
}

unsigned long CrossPointSettings::getSleepTimeoutMs() const {
  switch (sleepTimeout) {
    case SLEEP_1_MIN:
      return 1UL * 60 * 1000;
    case SLEEP_5_MIN:
      return 5UL * 60 * 1000;
    case SLEEP_10_MIN:
    default:
      return 10UL * 60 * 1000;
    case SLEEP_15_MIN:
      return 15UL * 60 * 1000;
    case SLEEP_30_MIN:
      return 30UL * 60 * 1000;
  }
}

int CrossPointSettings::getRefreshFrequency() const {
#if defined(BOARD_ESP32_S3_EPAPER_397)
  switch (refreshFrequency) {
    case REFRESH_45:
      return 45;
    case REFRESH_60:
      return 60;
    case REFRESH_30:
    default:
      return 30;
  }
#else
  switch (refreshFrequency) {
    case REFRESH_1:
      return 1;
    case REFRESH_5:
      return 5;
    case REFRESH_10:
      return 10;
    case REFRESH_15:
    default:
      return 15;
    case REFRESH_30:
      return 30;
  }
#endif
}

int CrossPointSettings::getReaderFontId() const {
  // Check SD card font first
  if (sdFontFamilyName[0] != '\0' && sdFontIdResolver) {
    int id = sdFontIdResolver(sdFontResolverCtx, sdFontFamilyName, fontSize);
    if (id != 0) return id;
    // Fall through to built-in if SD font not found
  }

  switch (fontFamily) {
    case NOTOSERIF:
    default:
      switch (fontSize) {
        case SMALL:
          return NOTOSERIF_12_FONT_ID;
        case MEDIUM:
        default:
          return NOTOSERIF_14_FONT_ID;
        case LARGE:
          return NOTOSERIF_16_FONT_ID;
        case EXTRA_LARGE:
          return NOTOSERIF_18_FONT_ID;
      }
    case NOTOSANS:
      switch (fontSize) {
        case SMALL:
          return NOTOSANS_12_FONT_ID;
        case MEDIUM:
        default:
          return NOTOSANS_14_FONT_ID;
        case LARGE:
          return NOTOSANS_16_FONT_ID;
        case EXTRA_LARGE:
          return NOTOSANS_18_FONT_ID;
      }
    case OPENDYSLEXIC:
      switch (fontSize) {
        case SMALL:
          return OPENDYSLEXIC_8_FONT_ID;
        case MEDIUM:
        default:
          return OPENDYSLEXIC_10_FONT_ID;
        case LARGE:
          return OPENDYSLEXIC_12_FONT_ID;
        case EXTRA_LARGE:
          return OPENDYSLEXIC_14_FONT_ID;
      }
  }
}

#if defined(BOARD_ESP32_S3_EPAPER_397)
#include <HalAudio397.h>

void CrossPointSettings::applyTimezoneToSystem() const {
  const long offsetSec = static_cast<long>(getEffectiveTimezoneOffsetMinutes()) * 60L;
  configTime(offsetSec, 0, "pool.ntp.org", "time.google.com");
}

void CrossPointSettings::applyOutputVolume() const { audio397.setOutputVolume(outputVolume); }

void CrossPointSettings::setOutputVolume(const uint8_t percent, const bool persist) {
  outputVolume = percent > 100 ? 100 : percent;
  applyOutputVolume();
  if (persist) {
    saveToFile();
  }
}
#endif
