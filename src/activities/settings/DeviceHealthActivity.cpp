#include "DeviceHealthActivity.h"

#if defined(BOARD_ESP32_S3_EPAPER_397)

#include <HalBoard397.h>
#include <HalStorage.h>
#include <HalTiltSensor.h>
#include <GfxRenderer.h>
#include <HalDisplay.h>
#include <I18n.h>

#include <cstdio>
#include <cmath>

#include <esp_heap_caps.h>

#include "ClockStore.h"
#include "MappedInputManager.h"
#include "activities/ActivityManager.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/TaskStackMonitor.h"
#include "util/UnifiedAppLayout.h"

void DeviceHealthActivity::onEnter() {
  Activity::onEnter();
  CLOCK_STORE.setAlarmCheckSuspended(true);
  lastRtcSecond = 255;
  requestUpdate();
}

void DeviceHealthActivity::onExit() {
  CLOCK_STORE.setAlarmCheckSuspended(false);
  Activity::onExit();
}

void DeviceHealthActivity::loop() {
  if (mappedInput.wasBackClicked() || mappedInput.wasConfirmClicked()) {
    finish();
    return;
  }
  if (UnifiedAppLayout::pollRtcSecondTick(lastRtcSecond)) {
    requestUpdate();
  }
}

void DeviceHealthActivity::render(RenderLock&&) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  const int pad = metrics.contentSidePadding;

  renderer.clearScreen();
  const int headerBottom = metrics.topPadding + metrics.headerHeight;
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_DEVICE_HEALTH));

  const auto layout =
      UnifiedAppLayout::splitBelowHeader(renderer, headerBottom, UnifiedAppLayout::kCompactBigTileHeight);
  UnifiedAppLayout::drawBigRtcClockTile(renderer, layout.bigTile);

  const int lineH = renderer.getLineHeight(UI_10_FONT_ID);
  const int infoBottom = layout.menu.y + layout.menu.height - 4;
  int y = layout.menu.y + 4;
  char line[96];

  auto drawInfoLine = [&](const char* text) {
    if (y + lineH > infoBottom) {
      return;
    }
    renderer.drawText(UI_10_FONT_ID, pad, y, text, true);
    y += lineH;
  };

#ifdef CROSSPOINT_VERSION
  snprintf(line, sizeof(line), "Firmware: %s", CROSSPOINT_VERSION);
  drawInfoLine(line);
#endif

  HalBoard397::DateTime rtc{};
  if (board397.readRtcForDisplay(rtc)) {
    snprintf(line, sizeof(line), "RTC: %02u/%02u/%04u %02u:%02u:%02u", rtc.day, rtc.month, rtc.year, rtc.hour,
             rtc.minute, rtc.second);
  } else {
    snprintf(line, sizeof(line), "RTC: %s", tr(STR_HEALTH_NOT_FOUND));
  }
  drawInfoLine(line);

  const uint8_t batt = board397.getBatteryPercent();
  snprintf(line, sizeof(line), "%s: %u%%%s%s", tr(STR_HEALTH_BATTERY), batt,
           board397.isCharging() ? " " : "", board397.isCharging() ? tr(STR_HEALTH_CHARGING) : "");
  drawInfoLine(line);

  HalBoard397::Environment env{};
  if (board397.peekEnvironmentCached(env) && std::isfinite(env.temperatureC) && std::isfinite(env.humidityPct)) {
    snprintf(line, sizeof(line), "SHTC3: %.1f C, %.1f %%RH", env.temperatureC, env.humidityPct);
  } else if (board397.hasEnvSensor()) {
    snprintf(line, sizeof(line), "SHTC3: %s", tr(STR_HEALTH_SENSOR_WAIT));
  } else {
    snprintf(line, sizeof(line), "SHTC3: %s", tr(STR_HEALTH_NOT_FOUND));
  }
  drawInfoLine(line);

  snprintf(line, sizeof(line), "QMI8658: %s",
           halTiltSensor.isAvailable() ? tr(STR_HEALTH_IMU_OK) : tr(STR_HEALTH_NOT_FOUND));
  drawInfoLine(line);

  snprintf(line, sizeof(line), "%s: %s", tr(STR_HEALTH_SD), Storage.ready() ? "OK" : "FAIL");
  drawInfoLine(line);

  const size_t internalFree = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
  snprintf(line, sizeof(line), "RAM: %u KB free", static_cast<unsigned>(internalFree / 1024));
  drawInfoLine(line);

  const size_t spiramFree = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
  snprintf(line, sizeof(line), "SPIRAM: %u KB free", static_cast<unsigned>(spiramFree / 1024));
  drawInfoLine(line);

  TaskStackMonitor::formatStackLine(line, sizeof(line), "Render", activityManager.getRenderTaskHandle());
  drawInfoLine(line);
  TaskStackMonitor::formatStackLine(line, sizeof(line), "Loop", xTaskGetCurrentTaskHandle());
  drawInfoLine(line);

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_OK_BUTTON), "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
}

#endif
