#pragma once

#include <cstdint>
#include <string>
#include <vector>

enum class AlarmRepeat : uint8_t { Once = 0, Daily = 1, Weekly = 2 };

struct ClockAlarm {
  bool enabled = false;
  uint8_t hour = 8;
  uint8_t minute = 0;
  AlarmRepeat repeat = AlarmRepeat::Daily;
  uint8_t weekdays = 0x7F;  // bit0=Sun .. bit6=Sat (Weekly only)
  bool onceFired = false;  // Once only: set when fired; enabled cleared; re-enable clears onceFired
  // Daily: fires every day at hour:minute while enabled.
  // Weekly: fires on selected weekdays (bit0=Sun..bit6=Sat); enabled cleared if no days selected.
  char label[24] = {};
  char soundFile[96] = {};
  uint16_t soundDurationSec = 60;
};

class ClockStore {
  static ClockStore instance;
  std::vector<ClockAlarm> alarms_;

 public:
  static constexpr int MAX_ALARMS = 5;
  static constexpr const char* FILE_PATH = "/.crosspoint/clock.json";

  static ClockStore& getInstance() { return instance; }

  bool loadFromFile();
  bool saveToFile();

  std::vector<ClockAlarm>& alarms() { return alarms_; }
  const std::vector<ClockAlarm>& alarms() const { return alarms_; }

  void ensureAlarmSlots();
  void normalizeAlarms();
  bool pollAlarmDue(int& outAlarmIndex, std::string& outLabel);
  void markAlarmTriggered(int alarmIndex);
  void setAlarmCheckSuspended(bool suspended);
  void resetAlarmMinuteLatch();
};

#define CLOCK_STORE ClockStore::getInstance()
