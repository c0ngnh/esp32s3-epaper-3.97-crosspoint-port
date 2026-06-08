#pragma once

#include <string>

struct BootHealthReport {
  bool settingsOk = false;
  bool stateOk = false;
  bool bookmarksOk = false;
  bool recentOk = false;
  bool sdOk = false;
  bool rtcOk = false;
  bool spiramOk = false;
  size_t spiramFree = 0;
  std::string settingsDetail;
  std::string stateDetail;
};

BootHealthReport runBootHealthCheck();
