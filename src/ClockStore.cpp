#include "ClockStore.h"

#include <ArduinoJson.h>
#include <HalBoard397.h>
#include <HalStorage.h>
#include <Logging.h>
#include <cstring>


ClockStore ClockStore::instance;

namespace {
bool alarmCheckSuspended = false;
int lastAlarmMinuteKey = -1;

int weekdaySun0(const int year, const int month, const int day) {
  int y = year;
  int m = month;
  if (m < 3) {
    m += 12;
    y--;
  }
  const int w = (y + y / 4 - y / 100 + y / 400 + (153 * m - 457) / 5 + day - 306) % 7;
  return (w + 7) % 7;
}

bool alarmMatchesNow(const ClockAlarm& alarm, const HalBoard397::DateTime& dt, const int wday) {
  if (!alarm.enabled) {
    return false;
  }
  if (alarm.hour != dt.hour || alarm.minute != dt.minute) {
    return false;
  }
  switch (alarm.repeat) {
    case AlarmRepeat::Once:
      // Fires at the next matching time, then markAlarmTriggered() clears enabled.
      return !alarm.onceFired;
    case AlarmRepeat::Daily:
      return true;
    case AlarmRepeat::Weekly:
      if (alarm.weekdays == 0) {
        return false;
      }
      return (alarm.weekdays & static_cast<uint8_t>(1u << wday)) != 0;
    default:
      return false;
  }
}

void normalizeAlarmInMemory(ClockAlarm& a) {
  if (a.repeat > AlarmRepeat::Weekly) {
    a.repeat = AlarmRepeat::Daily;
  }
  if (a.repeat == AlarmRepeat::Once && a.onceFired) {
    a.enabled = false;
  }
  if (a.repeat == AlarmRepeat::Weekly && a.weekdays == 0) {
    a.enabled = false;
  }
}
}  // namespace

void ClockStore::ensureAlarmSlots() {
  while (alarms_.size() < MAX_ALARMS) {
    alarms_.push_back(ClockAlarm{});
  }
  if (alarms_.size() > MAX_ALARMS) {
    alarms_.resize(MAX_ALARMS);
  }
}

bool ClockStore::loadFromFile() {
  alarms_.clear();
  ensureAlarmSlots();
  if (!Storage.exists(FILE_PATH)) {
    return saveToFile();
  }
  const String json = Storage.readFile(FILE_PATH);
  if (json.isEmpty()) {
    return false;
  }
  JsonDocument doc;
  if (deserializeJson(doc, json)) {
    return false;
  }
  alarms_.clear();
  for (JsonObject o : doc["alarms"].as<JsonArray>()) {
    ClockAlarm a;
    a.enabled = o["enabled"] | false;
    a.hour = o["hour"] | 8;
    a.minute = o["minute"] | 0;
    a.repeat = static_cast<AlarmRepeat>(o["repeat"] | static_cast<uint8_t>(AlarmRepeat::Daily));
    a.weekdays = o["weekdays"] | 0x7F;
    a.onceFired = o["onceFired"] | false;
    normalizeAlarmInMemory(a);
    const char* lbl = o["label"] | "";
    strncpy(a.label, lbl, sizeof(a.label) - 1);
    a.label[sizeof(a.label) - 1] = '\0';
    const char* snd = o["soundFile"] | "";
    strncpy(a.soundFile, snd, sizeof(a.soundFile) - 1);
    a.soundFile[sizeof(a.soundFile) - 1] = '\0';
    a.soundDurationSec = o["soundDurationSec"] | 60;
    if (a.soundDurationSec < 5) {
      a.soundDurationSec = 5;
    } else if (a.soundDurationSec > 600) {
      a.soundDurationSec = 600;
    }
    alarms_.push_back(a);
  }
  ensureAlarmSlots();
  return true;
}

bool ClockStore::saveToFile() {
  normalizeAlarms();
  Storage.mkdir("/.crosspoint");
  JsonDocument doc;
  JsonArray arr = doc["alarms"].to<JsonArray>();
  for (size_t i = 0; i < alarms_.size() && i < MAX_ALARMS; i++) {
    const auto& a = alarms_[i];
    JsonObject o = arr.add<JsonObject>();
    o["enabled"] = a.enabled;
    o["hour"] = a.hour;
    o["minute"] = a.minute;
    o["repeat"] = static_cast<uint8_t>(a.repeat);
    o["weekdays"] = a.weekdays;
    o["onceFired"] = a.onceFired;
    o["label"] = a.label;
    o["soundFile"] = a.soundFile;
    o["soundDurationSec"] = a.soundDurationSec;
  }
  String out;
  serializeJson(doc, out);
  return Storage.writeFile(FILE_PATH, out);
}

void ClockStore::normalizeAlarms() {
  for (ClockAlarm& a : alarms_) {
    normalizeAlarmInMemory(a);
  }
}

void ClockStore::setAlarmCheckSuspended(const bool suspended) { alarmCheckSuspended = suspended; }

void ClockStore::resetAlarmMinuteLatch() { lastAlarmMinuteKey = -1; }

void ClockStore::markAlarmTriggered(const int alarmIndex) {
  if (alarmIndex < 0 || alarmIndex >= static_cast<int>(alarms_.size())) {
    return;
  }
  ClockAlarm& a = alarms_[alarmIndex];
  switch (a.repeat) {
    case AlarmRepeat::Once:
      a.onceFired = true;
      a.enabled = false;
      saveToFile();
      break;
    case AlarmRepeat::Daily:
    case AlarmRepeat::Weekly:
      // Stay enabled; pollAlarmDue minute latch prevents re-triggering this minute.
      break;
    default:
      break;
  }
}

bool ClockStore::pollAlarmDue(int& outAlarmIndex, std::string& outLabel) {
#if !defined(BOARD_ESP32_S3_EPAPER_397)
  (void)outAlarmIndex;
  (void)outLabel;
  return false;
#else
  outAlarmIndex = -1;
  if (alarmCheckSuspended) {
    return false;
  }
  if (!board397.hasRtc()) {
    return false;
  }
  HalBoard397::DateTime dt{};
  if (!board397.readRtcForDisplay(dt)) {
    return false;
  }
  const int minuteKey = dt.hour * 60 + dt.minute;
  if (minuteKey == lastAlarmMinuteKey) {
    return false;
  }
  const int wday = weekdaySun0(dt.year, dt.month, dt.day);
  for (size_t i = 0; i < alarms_.size(); ++i) {
    const auto& a = alarms_[i];
    if (alarmMatchesNow(a, dt, wday)) {
      lastAlarmMinuteKey = minuteKey;
      outAlarmIndex = static_cast<int>(i);
      outLabel = a.label[0] ? a.label : "Alarm";
      return true;
    }
  }
  return false;
#endif
}
