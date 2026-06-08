#include "SettingsActivity.h"

#include <GfxRenderer.h>
#include <Logging.h>

#include <algorithm>

#include "ButtonRemapActivity.h"
#include "ClearCacheActivity.h"
#include "CrossPointSettings.h"
#include "HalGPIO.h"
#include "FontDownloadActivity.h"
#include "FontSelectionActivity.h"
#include "KOReaderSettingsActivity.h"
#include "LanguageSelectActivity.h"
#include "MappedInputManager.h"
#include "OpdsServerListActivity.h"
#include "OtaUpdateActivity.h"
#include "SdCardFontSystem.h"
#include "SdFirmwareUpdateActivity.h"
#include "DeviceSleep.h"
#include "activities/settings/DeviceHealthActivity.h"
#include "SettingsList.h"
#include "ClockSettingsActivity.h"
#if defined(BOARD_ESP32_S3_EPAPER_397)
#include <HalTimeSync397.h>
#endif
#include "StatusBarSettingsActivity.h"
#include "activities/network/WifiSelectionActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"

const StrId SettingsActivity::categoryNames[categoryCount] = {StrId::STR_CAT_DISPLAY, StrId::STR_CAT_READER,
                                                              StrId::STR_CAT_CONTROLS, StrId::STR_CAT_SYSTEM};

void SettingsActivity::rebuildSettingsLists() {
  displaySettings.clear();
  readerSettings.clear();
  controlsSettings.clear();
  systemSettings.clear();

  // Pick up any fonts uploaded/deleted over the web server since the last
  // reader activity ran — otherwise the font-family picker shows stale list.
  sdFontSystem.refreshIfDirty();

  for (auto& setting : getSettingsList(&sdFontSystem.registry())) {
    if (setting.category == StrId::STR_NONE_OPT) continue;
#if defined(BOARD_ESP32_S3_EPAPER_397)
    if (settingHiddenOnEpaper397(setting)) continue;
#endif
    if (setting.category == StrId::STR_CAT_DISPLAY) {
      displaySettings.push_back(setting);
    } else if (setting.category == StrId::STR_CAT_READER) {
      readerSettings.push_back(setting);
    } else if (setting.category == StrId::STR_CAT_CONTROLS) {
      controlsSettings.push_back(setting);
    } else if (setting.category == StrId::STR_CAT_SYSTEM) {
      systemSettings.push_back(setting);
    }
  }

  // Append device-only ACTION items
#if !defined(BOARD_ESP32_S3_EPAPER_397)
  controlsSettings.insert(controlsSettings.begin(),
                          SettingInfo::Action(StrId::STR_REMAP_FRONT_BUTTONS, SettingAction::RemapFrontButtons));
#endif
  systemSettings.push_back(SettingInfo::Action(StrId::STR_WIFI_NETWORKS, SettingAction::Network));
  systemSettings.push_back(SettingInfo::Action(StrId::STR_KOREADER_SYNC, SettingAction::KOReaderSync));
  systemSettings.push_back(SettingInfo::Action(StrId::STR_OPDS_SERVERS, SettingAction::OPDSBrowser));
  systemSettings.push_back(SettingInfo::Action(StrId::STR_CLEAR_READING_CACHE, SettingAction::ClearCache));
#if !defined(DISABLE_OTA_UPDATE)
  systemSettings.push_back(SettingInfo::Action(StrId::STR_CHECK_UPDATES, SettingAction::CheckForUpdates));
#endif
  systemSettings.push_back(SettingInfo::Action(StrId::STR_SD_FIRMWARE_UPDATE, SettingAction::SdFirmwareUpdate));
  systemSettings.push_back(SettingInfo::Action(StrId::STR_LANGUAGE, SettingAction::Language));
#if defined(BOARD_ESP32_S3_EPAPER_397)
  systemSettings.push_back(SettingInfo::Action(StrId::STR_DEVICE_HEALTH, SettingAction::DeviceHealth));
  systemSettings.push_back(
      SettingInfo::Action(StrId::STR_SET_DATE_TIME, SettingAction::ClockSettings)
          .withEnabledIf([] { return SETTINGS.autoTimeSync == 0; }));
  systemSettings.push_back(SettingInfo::Action(StrId::STR_POWER_OFF, SettingAction::PowerOff));
#endif
  // Insert "Manage Fonts" right after the font family setting so users discover it naturally
  readerSettings.insert(readerSettings.begin() + 1,
                        SettingInfo::Action(StrId::STR_MANAGE_FONTS, SettingAction::DownloadFonts));
  readerSettings.push_back(SettingInfo::Action(StrId::STR_CUSTOMISE_STATUS_BAR, SettingAction::CustomiseStatusBar));

  // Update currentSettings pointer and count for the active category
  switch (selectedCategoryIndex) {
    case 0:
      currentSettings = &displaySettings;
      break;
    case 1:
      currentSettings = &readerSettings;
      break;
    case 2:
      currentSettings = &controlsSettings;
      break;
    case 3:
      currentSettings = &systemSettings;
      break;
  }
  settingsCount = static_cast<int>(currentSettings->size());
  if (selectedSettingIndex > settingsCount) {
    selectedSettingIndex = settingsCount > 0 ? settingsCount : 0;
  }
}

void SettingsActivity::onEnter() {
  Activity::onEnter();

  // Reset selection to first category
  selectedCategoryIndex = 0;
  selectedSettingIndex = 0;
  editing = false;
  upHold_ = {};
  downHold_ = {};

  rebuildSettingsLists();

  // Trigger first update
  requestUpdate();
}

void SettingsActivity::onExit() {
  Activity::onExit();

  UITheme::getInstance().reload();  // Re-apply theme in case it was changed
}

bool SettingsActivity::settingOpensSubActivity(const SettingInfo& setting) const {
  return setting.type == SettingType::ACTION ||
         (setting.type == SettingType::ENUM && setting.nameId == StrId::STR_FONT_FAMILY);
}

void SettingsActivity::loopBrowseItems() {
  buttonNavigator.onNextRelease([this] {
    if (selectedSettingIndex == 0) {
      if (settingsCount > 0) {
        selectedSettingIndex = 1;
        requestUpdate();
      }
      return;
    }
    if (selectedSettingIndex < settingsCount) {
      selectedSettingIndex++;
      requestUpdate();
    }
  });

  buttonNavigator.onPreviousRelease([this] {
    if (selectedSettingIndex <= 1) {
      selectedSettingIndex = 0;
      requestUpdate();
      return;
    }
    selectedSettingIndex--;
    requestUpdate();
  });
}

void SettingsActivity::loopEdit() {
  const int selectedSetting = selectedSettingIndex - 1;
  if (selectedSetting < 0 || selectedSetting >= settingsCount) {
    return;
  }

  pollHoldAdjust(MappedInputManager::Button::Up, +1, upHold_);
  pollHoldAdjust(MappedInputManager::Button::Down, -1, downHold_);
}

int SettingsActivity::stepForRepeat(const int selectedSetting, const int repeatCount) const {
  if (selectedSetting < 0 || selectedSetting >= settingsCount) {
    return 1;
  }
  const auto& setting = (*currentSettings)[selectedSetting];
  if (setting.type == SettingType::TOGGLE) {
    return 1;
  }
  if (setting.type == SettingType::VALUE) {
    int step = 5 + (repeatCount / 3) * 5;
    return std::min(step, static_cast<int>(setting.valueRange.max - setting.valueRange.min));
  }
  return 1;
}

unsigned long SettingsActivity::intervalForRepeat(const int repeatCount) const {
  unsigned long interval = HOLD_INTERVAL_MAX_MS;
  if (repeatCount > 6) {
    interval = 120;
  }
  if (repeatCount > 12) {
    interval = 90;
  }
  if (repeatCount > 20) {
    interval = 65;
  }
  if (repeatCount > 28) {
    interval = HOLD_INTERVAL_MIN_MS;
  }
  return interval;
}

void SettingsActivity::pollHoldAdjust(const MappedInputManager::Button button, const int direction,
                                        HoldAdjustState& state) {
  const bool isUp = button == MappedInputManager::Button::Up;
  const bool isDown = button == MappedInputManager::Button::Down;
  if (!isUp && !isDown) {
    return;
  }

  const int selectedSetting = selectedSettingIndex - 1;

  if (mappedInput.wasPressed(button)) {
    state.lastRepeatMs = 0;
    state.repeatCount = 0;
    state.didRepeat = false;
  }

  if (mappedInput.wasReleased(button)) {
    if (!state.didRepeat) {
      adjustSetting(selectedSetting, direction, 1);
      requestUpdate();
    }
    state.lastRepeatMs = 0;
    state.repeatCount = 0;
    state.didRepeat = false;
    return;
  }

  if (!mappedInput.isPressed(button)) {
    return;
  }

  if (mappedInput.getHeldTime() < HOLD_START_MS) {
    return;
  }

  const uint32_t now = millis();
  if (state.lastRepeatMs == 0) {
    state.lastRepeatMs = now;
    state.repeatCount = 0;
  }

  if (now - state.lastRepeatMs < intervalForRepeat(static_cast<int>(state.repeatCount))) {
    return;
  }

  const int step = stepForRepeat(selectedSetting, static_cast<int>(state.repeatCount));
  adjustSetting(selectedSetting, direction, step);
  state.didRepeat = true;
  state.repeatCount++;
  state.lastRepeatMs = now;
  requestUpdate();
}

void SettingsActivity::adjustSetting(const int selectedSetting, const int delta, const int step) {
  if (selectedSetting < 0 || selectedSetting >= settingsCount || delta == 0 || step <= 0) {
    return;
  }

  const auto& setting = (*currentSettings)[selectedSetting];
  if (!setting.enabled() || settingOpensSubActivity(setting)) {
    return;
  }

  const int change = delta * step;

  if (setting.type == SettingType::TOGGLE && setting.valuePtr != nullptr) {
    SETTINGS.*(setting.valuePtr) = change > 0;
#if defined(BOARD_ESP32_S3_EPAPER_397)
    if (setting.nameId == StrId::STR_AUTO_TIME_SYNC && SETTINGS.autoTimeSync != 0) {
      HalTimeSync397::requestResync();
    }
#endif
  } else if (setting.type == SettingType::ENUM && setting.valuePtr != nullptr) {
    const int count = static_cast<int>(setting.enumValues.size());
    if (count > 0) {
      int value = static_cast<int>(SETTINGS.*(setting.valuePtr));
      value = (value + change % count + count) % count;
      SETTINGS.*(setting.valuePtr) = static_cast<uint8_t>(value);
    }
  } else if (setting.type == SettingType::ENUM && setting.valueGetter && setting.valueSetter) {
    const uint8_t totalValues = setting.enumStringValues.empty()
                                    ? static_cast<uint8_t>(setting.enumValues.size())
                                    : static_cast<uint8_t>(setting.enumStringValues.size());
    if (totalValues > 0) {
      int cur = static_cast<int>(setting.valueGetter());
      cur = (cur + change % static_cast<int>(totalValues) + static_cast<int>(totalValues)) %
            static_cast<int>(totalValues);
      setting.valueSetter(static_cast<uint8_t>(cur));
    }
  } else if (setting.type == SettingType::VALUE && setting.valuePtr != nullptr) {
    int value = static_cast<int>(SETTINGS.*(setting.valuePtr)) + change;
    if (value > setting.valueRange.max) {
      value = setting.valueRange.max;
    }
    if (value < setting.valueRange.min) {
      value = setting.valueRange.min;
    }
    SETTINGS.*(setting.valuePtr) = static_cast<uint8_t>(value);
  } else {
    return;
  }

  SETTINGS.saveToFile();
}

void SettingsActivity::loop() {
  if (mappedInput.wasBackClicked()) {
    if (editing) {
      editing = false;
      upHold_ = {};
      downHold_ = {};
      requestUpdate();
      return;
    }
    if (selectedSettingIndex > 0) {
      selectedSettingIndex = 0;
      requestUpdate();
      return;
    }
    SETTINGS.saveToFile();
    activityManager.goHome();
    return;
  }

#if defined(BOARD_ESP32_S3_EPAPER_397)
  if (consumeConfirmClick()) {
#else
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
#endif
    if (editing) {
      editing = false;
      upHold_ = {};
      downHold_ = {};
      SETTINGS.saveToFile();
      requestUpdate();
      return;
    }
    if (selectedSettingIndex == 0) {
      selectedCategoryIndex = (selectedCategoryIndex < categoryCount - 1) ? (selectedCategoryIndex + 1) : 0;
      rebuildSettingsLists();
      requestUpdate();
      return;
    }

    const int selectedSetting = selectedSettingIndex - 1;
    if (selectedSetting >= 0 && selectedSetting < settingsCount) {
      const auto& setting = (*currentSettings)[selectedSetting];
      if (setting.enabled()) {
        if (settingOpensSubActivity(setting)) {
          activateSetting(selectedSetting);
        } else {
          editing = true;
          upHold_ = {};
          downHold_ = {};
          requestUpdate();
        }
      }
    }
    return;
  }

  if (editing) {
    loopEdit();
    return;
  }

  loopBrowseItems();
}

void SettingsActivity::activateSetting(const int selectedSetting) {
  if (selectedSetting < 0 || selectedSetting >= settingsCount) {
    return;
  }

  const auto& setting = (*currentSettings)[selectedSetting];

  if (!setting.enabled()) {
    return;
  }

  if (setting.type == SettingType::ACTION) {
    auto resultHandler = [this](const ActivityResult&) { SETTINGS.saveToFile(); };

    switch (setting.action) {
      case SettingAction::RemapFrontButtons:
        startActivityForResult(std::make_unique<ButtonRemapActivity>(renderer, mappedInput), resultHandler);
        break;
      case SettingAction::CustomiseStatusBar:
        startActivityForResult(std::make_unique<StatusBarSettingsActivity>(renderer, mappedInput), resultHandler);
        break;
      case SettingAction::KOReaderSync:
        startActivityForResult(std::make_unique<KOReaderSettingsActivity>(renderer, mappedInput), resultHandler);
        break;
      case SettingAction::OPDSBrowser:
        startActivityForResult(std::make_unique<OpdsServerListActivity>(renderer, mappedInput), resultHandler);
        break;
      case SettingAction::Network:
        startActivityForResult(std::make_unique<WifiSelectionActivity>(renderer, mappedInput, false), resultHandler);
        break;
      case SettingAction::ClearCache:
        startActivityForResult(std::make_unique<ClearCacheActivity>(renderer, mappedInput), resultHandler);
        break;
#if !defined(DISABLE_OTA_UPDATE)
      case SettingAction::CheckForUpdates:
        startActivityForResult(std::make_unique<OtaUpdateActivity>(renderer, mappedInput), resultHandler);
        break;
#endif
      case SettingAction::SdFirmwareUpdate:
        startActivityForResult(std::make_unique<SdFirmwareUpdateActivity>(renderer, mappedInput), resultHandler);
        break;
      case SettingAction::DownloadFonts:
        startActivityForResult(std::make_unique<FontDownloadActivity>(renderer, mappedInput),
                               [this](const ActivityResult&) {
                                 SETTINGS.saveToFile();
                                 rebuildSettingsLists();
                               });
        break;
      case SettingAction::Language:
        startActivityForResult(std::make_unique<LanguageSelectActivity>(renderer, mappedInput), resultHandler);
        break;
#if defined(BOARD_ESP32_S3_EPAPER_397)
      case SettingAction::ClockSettings:
        startActivityForResult(std::make_unique<ClockSettingsActivity>(renderer, mappedInput), resultHandler);
        break;
      case SettingAction::DeviceHealth:
        startActivityForResult(std::make_unique<DeviceHealthActivity>(renderer, mappedInput),
                               [](const ActivityResult&) {});
        break;
      case SettingAction::PowerOff:
        requestDeepSleep(true);
        break;
#endif
      case SettingAction::None:
        // Do nothing
        break;
    }
    return;
  }

  if (setting.type == SettingType::ENUM && setting.valueGetter && setting.nameId == StrId::STR_FONT_FAMILY) {
    startActivityForResult(std::make_unique<FontSelectionActivity>(renderer, mappedInput, &sdFontSystem.registry()),
                           [this](const ActivityResult&) {
                             SETTINGS.saveToFile();
                             rebuildSettingsLists();
                           });
  }
}

void SettingsActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  const auto& metrics = UITheme::getInstance().getMetrics();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_SETTINGS_TITLE),
                 CROSSPOINT_VERSION);

  std::vector<TabInfo> tabs;
  tabs.reserve(categoryCount);
  for (int i = 0; i < categoryCount; i++) {
    tabs.push_back({I18N.get(categoryNames[i]), selectedCategoryIndex == i});
  }
  GUI.drawTabBar(renderer, Rect{0, metrics.topPadding + metrics.headerHeight, pageWidth, metrics.tabBarHeight}, tabs,
                 selectedSettingIndex == 0);

  const auto& settings = *currentSettings;
  GUI.drawList(
      renderer,
      Rect{0, metrics.topPadding + metrics.headerHeight + metrics.tabBarHeight + metrics.verticalSpacing, pageWidth,
           pageHeight - (metrics.topPadding + metrics.headerHeight + metrics.tabBarHeight + metrics.buttonHintsHeight +
                         metrics.verticalSpacing * 2)},
      settingsCount, selectedSettingIndex - 1,
      [&settings](int index) { return std::string(I18N.get(settings[index].nameId)); }, nullptr, nullptr,
      [&settings](int i) {
        const auto& setting = settings[i];
        std::string valueText = "";
        if (setting.type == SettingType::TOGGLE && setting.valuePtr != nullptr) {
          const bool value = SETTINGS.*(setting.valuePtr);
          valueText = value ? tr(STR_STATE_ON) : tr(STR_STATE_OFF);
        } else if (setting.type == SettingType::ENUM && setting.valuePtr != nullptr) {
          const uint8_t value = SETTINGS.*(setting.valuePtr);
          valueText = I18N.get(setting.enumValues[value]);
        } else if (setting.type == SettingType::ENUM && setting.valueGetter) {
          const uint8_t value = setting.valueGetter();
          if (!setting.enumStringValues.empty() && value < setting.enumStringValues.size()) {
            valueText = setting.enumStringValues[value];
          } else if (value < setting.enumValues.size()) {
            valueText = I18N.get(setting.enumValues[value]);
          }
        } else if (setting.type == SettingType::VALUE && setting.valuePtr != nullptr) {
          valueText = std::to_string(SETTINGS.*(setting.valuePtr));
        }
        return valueText;
      },
      editing,
      [&settings](int i) { return !settings[i].enabled(); });

  MappedInputManager::Labels labels;
  if (editing) {
    labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_DONE), "+", "-");
  } else if (selectedSettingIndex == 0) {
    const auto confirmLabel = I18N.get(categoryNames[(selectedCategoryIndex + 1) % categoryCount]);
    labels = mappedInput.mapLabels(tr(STR_BACK), confirmLabel, tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  } else {
    labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_EDIT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  }
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  // Always use standard refresh for settings screen
  renderer.displayBuffer();
}
