#include "SdFirmwareUpdateActivity.h"

#include <Arduino.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>

#include <vector>

#include "MappedInputManager.h"
#include "activities/home/FileBrowserActivity.h"
#include "activities/util/ConfirmationActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "network/FirmwareFlasher.h"
#include "util/AppScreenLayout.h"

namespace {
void drawTitleAndWrappedBody(const GfxRenderer& renderer, const Rect& body, const char* title, const char* detail) {
  if (body.height <= 0) {
    return;
  }
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int lineH = renderer.getLineHeight(UI_10_FONT_ID);
  const int pad = metrics.contentSidePadding;
  const int maxWidth = std::max(0, body.width - pad * 2);

  std::vector<std::string> detailLines;
  if (detail != nullptr && detail[0] != '\0') {
    detailLines = renderer.wrappedText(UI_10_FONT_ID, detail, maxWidth, 8, EpdFontFamily::REGULAR);
  }

  const int detailHeight = static_cast<int>(detailLines.size()) * lineH;
  const int gap = detailLines.empty() ? 0 : metrics.verticalSpacing;
  const int blockHeight = lineH + gap + detailHeight;
  int y = body.y + std::max(0, (body.height - blockHeight) / 2);

  if (title != nullptr && title[0] != '\0') {
    renderer.drawCenteredText(UI_10_FONT_ID, y, title, true, EpdFontFamily::BOLD);
    y += lineH + gap;
  }

  for (const auto& line : detailLines) {
    renderer.drawText(UI_10_FONT_ID, body.x + pad, y, line.c_str(), true, EpdFontFamily::REGULAR);
    y += lineH;
  }
}
}  // namespace

void SdFirmwareUpdateActivity::onEnter() {
  Activity::onEnter();
  // Build-identity marker — confirms which firmware build owns the SD update flow.
  LOG_INF("FW", "SdFirmwareUpdateActivity build=%s %s recovery=%d", __DATE__, __TIME__, recoveryMode ? 1 : 0);
  state = State::PICKING;
  launchPicker();
}

void SdFirmwareUpdateActivity::launchPicker() {
  // Reuse the standard file browser, restricted to .bin files only.
  startActivityForResult(
      std::make_unique<FileBrowserActivity>(renderer, mappedInput, "/", FileBrowserActivity::Mode::PickFirmware),
      [this](const ActivityResult& result) { onPickerResult(result); });
}

void SdFirmwareUpdateActivity::onPickerResult(const ActivityResult& result) {
  if (result.isCancelled) {
    if (recoveryMode) {
      // Recovery mode: re-launch the picker so the user cannot escape into a half-initialised UI.
      launchPicker();
      return;
    }
    finish();
    return;
  }

  const auto* path = std::get_if<FilePathResult>(&result.data);
  if (!path) {
    LOG_ERR("FW", "Picker returned no path");
    finish();
    return;
  }
  firmwarePath = path->path;
  LOG_DBG("FW", "Selected: %s", firmwarePath.c_str());

  {
    RenderLock lock(*this);
    state = State::VALIDATING;
  }
  requestUpdateAndWait();

  if (!validateFirmware()) {
    RenderLock lock(*this);
    state = State::FAILED;
    requestUpdate();
    return;
  }

  promptConfirmation();
}

bool SdFirmwareUpdateActivity::validateFirmware() {
  HalFile file;
  if (!Storage.openFileForRead("FW", firmwarePath.c_str(), file) || !file) {
    errorMessage = tr(STR_FIRMWARE_FILE_OPEN_FAILED);
    return false;
  }
  firmwareSize = file.fileSize();
  file.close();

  // Resolve the inactive OTA slot (dual-bank). Single-bank layouts cannot SD-update safely.
  if (!firmware_flash::hasDualOtaAppPartitions()) {
    LOG_ERR("FW", "partition table lacks dual OTA app slots");
    errorMessage = tr(STR_FIRMWARE_USB_FLASH_REQUIRED);
    return false;
  }
  const esp_partition_t* dest = firmware_flash::getUpdatePartition();
  if (!dest) {
    LOG_ERR("FW", "no inactive OTA partition available");
    errorMessage = tr(STR_FIRMWARE_USB_FLASH_REQUIRED);
    return false;
  }
  const size_t partitionLimit = dest->size;
  if (firmwareSize > partitionLimit) {
    LOG_ERR("FW", "firmware (%u bytes) exceeds partition (%u bytes)", static_cast<unsigned>(firmwareSize),
            static_cast<unsigned>(partitionLimit));
    errorMessage = tr(STR_FIRMWARE_TOO_LARGE);
    return false;
  }

  // Run the same end-to-end integrity check (header / segment table / XOR checksum / SHA256
  // trailer) that the shared firmware-flasher applies right before raw-writing otadata. This
  // catches truncated or corrupted .bin files at confirmation time, before the user ever sees
  // the "Updating…" progress bar.
  const auto vr = firmware_flash::validateSdUpdateImage(firmwarePath.c_str(), partitionLimit);
  if (vr != firmware_flash::Result::OK) {
    LOG_ERR("FW", "image validation failed: %s", firmware_flash::resultName(vr));
    if (vr == firmware_flash::Result::TOO_LARGE) {
      errorMessage = tr(STR_FIRMWARE_TOO_LARGE);
    } else if (vr == firmware_flash::Result::TOO_SMALL) {
      errorMessage = tr(STR_FIRMWARE_TOO_SMALL);
    } else if (vr == firmware_flash::Result::WRONG_IMAGE_TYPE) {
      errorMessage = tr(STR_FIRMWARE_WRONG_IMAGE_TYPE);
    } else {
      errorMessage = tr(STR_INVALID_FIRMWARE);
    }
    return false;
  }
  return true;
}

void SdFirmwareUpdateActivity::promptConfirmation() {
  {
    RenderLock lock(*this);
    state = State::CONFIRMING;
  }
  // Show "Update firmware?" with the file path as the body line.
  std::string heading = tr(STR_FIRMWARE_UPDATE_PROMPT);
  // Use the basename only to keep the body short.
  std::string body = firmwarePath;
  const auto pos = body.find_last_of('/');
  if (pos != std::string::npos) body = body.substr(pos + 1);

  startActivityForResult(
      std::make_unique<ConfirmationActivity>(renderer, mappedInput, heading, body, false, 3, true),
      [this](const ActivityResult& result) { onConfirmationResult(result); });
}

void SdFirmwareUpdateActivity::onConfirmationResult(const ActivityResult& result) {
  if (result.isCancelled) {
    if (recoveryMode) {
      // Go back to the picker rather than exiting recovery.
      launchPicker();
      return;
    }
    finish();
    return;
  }

  {
    RenderLock lock(*this);
    state = State::UPDATING;
    writtenBytes = 0;
    lastRenderedPercent = 101;
  }
  requestUpdateAndWait();
  performUpdate();
}

void SdFirmwareUpdateActivity::performUpdate() {
  LOG_INF("FW", "SD update: %s (%u bytes)", firmwarePath.c_str(), static_cast<unsigned>(firmwareSize));

  auto progressCb = +[](size_t written, size_t total, void* ctx) {
    auto* self = static_cast<SdFirmwareUpdateActivity*>(ctx);
    self->writtenBytes = written;
    self->firmwareSize = total;
    // immediate=true: wake the render task directly. We're in a tight sync
    // loop so the main loop won't drain the requestedUpdate flag for us.
    self->requestUpdate(true);
  };

  // Re-validate at flash time (TOCTOU): SD is removable, so don't trust the
  // pre-confirmation pass. The alreadyValidated parameter on the API stays
  // for callers (e.g. an OTA staging path) where the same byte stream was
  // just hashed and there's no removable-media gap.
  const auto result = firmware_flash::flashFromSdPath(firmwarePath.c_str(), progressCb, this);
  if (result != firmware_flash::Result::OK) {
    LOG_ERR("FW", "flash failed: %s", firmware_flash::resultName(result));
    if (result == firmware_flash::Result::INPLACE_NOT_SUPPORTED) {
      errorMessage = tr(STR_FIRMWARE_USB_FLASH_REQUIRED);
    } else {
      errorMessage = tr(STR_FIRMWARE_WRITE_FAILED);
    }
    RenderLock lock(*this);
    state = State::FAILED;
    requestUpdate();
    return;
  }

  LOG_INF("FW", "SD firmware update complete, restarting");
  {
    RenderLock lock(*this);
    state = State::SUCCESS;
  }
  requestUpdateAndWait();
  delay(1500);
  ESP.restart();
}

void SdFirmwareUpdateActivity::loop() {
  if (state == State::FAILED) {
    if (mappedInput.wasBackClicked()
#if defined(BOARD_ESP32_S3_EPAPER_397)
        || consumeConfirmClick()
#else
        || mappedInput.wasReleased(MappedInputManager::Button::Confirm)
#endif
    ) {
      if (recoveryMode) {
        // Go back to picker so user can try a different .bin
        state = State::PICKING;
        launchPicker();
        return;
      }
      finish();
    }
  }
}

void SdFirmwareUpdateActivity::render(RenderLock&&) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto screen = AppScreenLayout::listScreen(renderer);

  renderer.clearScreen();

  const char* headerText = recoveryMode ? tr(STR_RECOVERY_MODE) : tr(STR_SD_FIRMWARE_UPDATE);
  GUI.drawHeader(renderer, screen.header, headerText);

  const auto lineHeight = renderer.getLineHeight(UI_10_FONT_ID);

  if (state == State::VALIDATING) {
    AppScreenLayout::drawBodyMessage(renderer, screen.body, tr(STR_VALIDATING_FIRMWARE));
  } else if (state == State::UPDATING) {
    const unsigned int pct = firmwareSize > 0 ? static_cast<unsigned int>((writtenBytes * 100) / firmwareSize) : 0;
    lastRenderedPercent = pct;

    const int blockHeight = lineHeight + metrics.verticalSpacing + metrics.progressBarHeight + metrics.verticalSpacing +
                            lineHeight + metrics.verticalSpacing + lineHeight;
    int y = screen.body.y + std::max(0, (screen.body.height - blockHeight) / 2);

    renderer.drawCenteredText(UI_10_FONT_ID, y, tr(STR_UPDATING), true, EpdFontFamily::BOLD);
    y += lineHeight + metrics.verticalSpacing;
    GUI.drawProgressBar(
        renderer,
        Rect{metrics.contentSidePadding, y, screen.body.width - metrics.contentSidePadding * 2, metrics.progressBarHeight},
        static_cast<int>(pct), 100);
    y += metrics.progressBarHeight + metrics.verticalSpacing + lineHeight + metrics.verticalSpacing;
    renderer.drawCenteredText(UI_10_FONT_ID, y, tr(STR_FIRMWARE_UPDATE_DO_NOT_POWER_OFF));
  } else if (state == State::SUCCESS) {
    drawTitleAndWrappedBody(renderer, screen.body, tr(STR_UPDATE_COMPLETE), tr(STR_RESTARTING_HINT));
  } else if (state == State::FAILED) {
    drawTitleAndWrappedBody(renderer, screen.body, tr(STR_UPDATE_FAILED),
                            errorMessage.empty() ? nullptr : errorMessage.c_str());
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  } else {
    if (recoveryMode) {
      AppScreenLayout::drawBodyMessage(renderer, screen.body, tr(STR_RECOVERY_MODE_HINT));
    }
  }

  renderer.displayBuffer();
}
