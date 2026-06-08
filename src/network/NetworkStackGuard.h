#pragma once

#include <Arduino.h>
#include <ESPmDNS.h>
#include <WiFi.h>

// Serialize lwIP UDP/mDNS use around WiFi connect/disconnect to avoid
// "Required to lock TCPIP core functionality" asserts on IDF 5.x builds.
namespace NetworkStackGuard {

inline void stopMdns() {
  MDNS.end();
  delay(20);
}

inline void prepareForWifiReconnect() {
  stopMdns();
  delay(30);
}

inline bool waitForStaIp(const unsigned long timeoutMs = 4000) {
  const unsigned long start = millis();
  while (millis() - start < timeoutMs) {
    if (WiFi.status() == WL_CONNECTED && WiFi.localIP() != INADDR_NONE) {
      return true;
    }
    delay(50);
    yield();
  }
  return WiFi.status() == WL_CONNECTED && WiFi.localIP() != INADDR_NONE;
}

inline void settleLwipStack(const unsigned long extraMs = 150) {
  delay(extraMs);
  yield();
}

inline bool beginMdns(const char* hostname) {
  settleLwipStack(100);
  stopMdns();
  settleLwipStack(50);
  return MDNS.begin(hostname);
}

inline bool isNetworkReadyForUdp(const bool apMode) {
  if (apMode) {
    return WiFi.softAPIP() != INADDR_NONE;
  }
  return WiFi.status() == WL_CONNECTED && WiFi.localIP() != INADDR_NONE;
}

}  // namespace
