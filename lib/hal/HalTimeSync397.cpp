#include "HalTimeSync397.h"

#if defined(BOARD_ESP32_S3_EPAPER_397)

#include <CrossPointSettings.h>
#include <HalBoard397.h>
#include <HTTPClient.h>
#include <Logging.h>
#include <WiFi.h>

#include <ArduinoJson.h>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <ctime>

#include <esp_task_wdt.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

namespace {

std::atomic<bool> syncedThisConnection{false};
TaskHandle_t syncTaskHandle = nullptr;

bool timeLooksValid(const tm& timeinfo) {
  return timeinfo.tm_year + 1900 >= 2020;
}

bool parseUtcOffsetMinutes(const char* offsetStr, int16_t* outMinutes) {
  if (offsetStr == nullptr || outMinutes == nullptr || offsetStr[0] != '+' && offsetStr[0] != '-') {
    return false;
  }
  const int sign = offsetStr[0] == '-' ? -1 : 1;
  int hours = 0;
  int minutes = 0;
  if (std::sscanf(offsetStr + 1, "%d:%d", &hours, &minutes) < 1) {
    return false;
  }
  *outMinutes = static_cast<int16_t>(sign * (hours * 60 + minutes));
  return true;
}

bool fetchTimezoneFromIp(int16_t* outMinutes) {
  if (WiFi.status() != WL_CONNECTED) {
    return false;
  }

  WiFiClient client;
  HTTPClient http;
  // Free geolocation API: offset in seconds from UTC (e.g. 25200 = UTC+7).
  if (!http.begin(client, "http://ip-api.com/json/?fields=status,offset,timezone")) {
    return false;
  }
  http.setTimeout(8000);
  const int code = http.GET();
  if (code != HTTP_CODE_OK) {
    http.end();
    return false;
  }

  JsonDocument doc;
  const String body = http.getString();
  http.end();

  if (deserializeJson(doc, body) != DeserializationError::Ok) {
    return false;
  }
  if (std::strcmp(doc["status"] | "", "success") != 0) {
    return false;
  }

  const int offsetSec = doc["offset"] | 0;
  *outMinutes = static_cast<int16_t>(offsetSec / 60);
  const char* tzName = doc["timezone"] | "";
  if (tzName[0] != '\0') {
    LOG_INF("TIME", "Timezone from IP: %s (UTC%+d:%02d)", tzName, *outMinutes / 60,
            abs(*outMinutes % 60));
  }
  return true;
}

bool fetchWorldTimeApi(HalBoard397::DateTime& outLocal, int16_t* outOffsetMinutes) {
  if (WiFi.status() != WL_CONNECTED) {
    return false;
  }

  WiFiClient client;
  HTTPClient http;
  if (!http.begin(client, "http://worldtimeapi.org/api/ip")) {
    return false;
  }
  http.setTimeout(8000);
  const int code = http.GET();
  if (code != HTTP_CODE_OK) {
    http.end();
    return false;
  }

  JsonDocument doc;
  const String body = http.getString();
  http.end();

  if (deserializeJson(doc, body) != DeserializationError::Ok) {
    return false;
  }

  const char* offsetStr = doc["utc_offset"] | "";
  if (!parseUtcOffsetMinutes(offsetStr, outOffsetMinutes)) {
    const int offsetSec = doc["raw_offset"] | 0;
    *outOffsetMinutes = static_cast<int16_t>(offsetSec / 60);
  }

  const char* datetime = doc["datetime"] | "";
  if (datetime[0] == '\0') {
    return false;
  }

  int year = 0;
  int month = 0;
  int day = 0;
  int hour = 0;
  int minute = 0;
  int second = 0;
  if (std::sscanf(datetime, "%d-%d-%dT%d:%d:%d", &year, &month, &day, &hour, &minute, &second) < 6) {
    return false;
  }

  outLocal.year = static_cast<uint16_t>(year);
  outLocal.month = static_cast<uint8_t>(month);
  outLocal.day = static_cast<uint8_t>(day);
  outLocal.hour = static_cast<uint8_t>(hour);
  outLocal.minute = static_cast<uint8_t>(minute);
  outLocal.second = static_cast<uint8_t>(second);
  return true;
}

void applyLocalTimeToRtc(const HalBoard397::DateTime& dt) {
  if (!board397.hasRtc()) {
    return;
  }
  if (board397.setRtc(dt)) {
    LOG_INF("WS397", "RTC set to local time %04u-%02u-%02u %02u:%02u:%02u", dt.year, dt.month, dt.day, dt.hour,
            dt.minute, dt.second);
  }
}

void ntpSyncTask(void*) {
  if (!SETTINGS.autoTimeSync) {
    syncTaskHandle = nullptr;
    vTaskDelete(nullptr);
    return;
  }

  int16_t offsetMinutes = SETTINGS.timezoneOffsetMinutes;
  HalBoard397::DateTime localFromApi{};
  bool haveLocal = fetchWorldTimeApi(localFromApi, &offsetMinutes);

  if (!haveLocal) {
    if (!fetchTimezoneFromIp(&offsetMinutes)) {
      LOG_DBG("TIME", "IP timezone lookup failed; using saved offset %d min", static_cast<int>(offsetMinutes));
    }
  }

  if (offsetMinutes < -720) {
    offsetMinutes = -720;
  } else if (offsetMinutes > 840) {
    offsetMinutes = 840;
  }

  SETTINGS.timezoneOffsetMinutes = offsetMinutes;
  SETTINGS.applyTimezoneToSystem();
  SETTINGS.saveToFile();

  if (haveLocal) {
    applyLocalTimeToRtc(localFromApi);
    syncedThisConnection.store(true);
    syncTaskHandle = nullptr;
    vTaskDelete(nullptr);
    return;
  }

  tm timeinfo{};
  bool gotTime = false;
  for (int attempt = 0; attempt < 20; ++attempt) {
    esp_task_wdt_reset();
    if (getLocalTime(&timeinfo, 2000) && timeLooksValid(timeinfo)) {
      gotTime = true;
      break;
    }
    vTaskDelay(pdMS_TO_TICKS(250));
  }

  if (gotTime && board397.hasRtc()) {
    HalBoard397::DateTime dt{};
    dt.year = static_cast<uint16_t>(timeinfo.tm_year + 1900);
    dt.month = static_cast<uint8_t>(timeinfo.tm_mon + 1);
    dt.day = static_cast<uint8_t>(timeinfo.tm_mday);
    dt.hour = static_cast<uint8_t>(timeinfo.tm_hour);
    dt.minute = static_cast<uint8_t>(timeinfo.tm_min);
    dt.second = static_cast<uint8_t>(timeinfo.tm_sec);
    applyLocalTimeToRtc(dt);
    LOG_INF("TIME", "RTC set from NTP (local UTC%+d): %04u-%02u-%02u %02u:%02u:%02u", offsetMinutes / 60, dt.year,
            dt.month, dt.day, dt.hour, dt.minute, dt.second);
    syncedThisConnection.store(true);
  } else if (!gotTime) {
    LOG_ERR("TIME", "NTP time sync failed");
  }

  syncTaskHandle = nullptr;
  vTaskDelete(nullptr);
}

}  // namespace

void HalTimeSync397::requestResync() {
  syncedThisConnection.store(false);
}

void HalTimeSync397::cancelBeforeDeepSleep() {
  syncedThisConnection.store(false);
  WiFi.disconnect(true, true);
  WiFi.mode(WIFI_OFF);
}

void HalTimeSync397::poll() {
  if (!SETTINGS.autoTimeSync) {
    return;
  }

  const bool connected = WiFi.status() == WL_CONNECTED;
  static bool wasConnected = false;

  if (!connected) {
    wasConnected = false;
    syncedThisConnection.store(false);
    return;
  }

  if (wasConnected && syncedThisConnection.load()) {
    return;
  }

  wasConnected = true;

  if (!board397.hasRtc() || syncTaskHandle != nullptr) {
    return;
  }

  if (xTaskCreate(ntpSyncTask, "ntpSync", 8192, nullptr, 1, &syncTaskHandle) != pdPASS) {
    LOG_ERR("TIME", "Failed to start NTP sync task");
    syncTaskHandle = nullptr;
  }
}

#endif
