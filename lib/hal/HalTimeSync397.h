#pragma once

#if defined(BOARD_ESP32_S3_EPAPER_397)

// Sync PCF85063 RTC from NTP when WiFi connects (no user prompt).
namespace HalTimeSync397 {
void poll();
// Allow a new WiFi time sync (e.g. after enabling auto-update in settings).
void requestResync();
// Stop WiFi/NTP work before deep sleep.
void cancelBeforeDeepSleep();
}

#endif
