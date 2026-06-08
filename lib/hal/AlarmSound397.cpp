#include "AlarmSound397.h"

#if defined(BOARD_ESP32_S3_EPAPER_397)

#include <AudioFilePlayer.h>
#include <HalAudio397.h>
#include <Logging.h>

#include <atomic>
#include <cmath>
#include <cstdlib>
#include <string>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "CrossPointSettings.h"

namespace {

constexpr uint32_t kSampleRate = HalAudio397::DEFAULT_SAMPLE_RATE;
constexpr size_t kChunkSamples = 512;
constexpr float kToneAHz = 880.0f;
constexpr float kToneBHz = 988.0f;
constexpr int16_t kAmplitude = 12000;

std::atomic<bool> g_stop{false};
std::atomic<bool> g_beepPlaying{false};
std::atomic<uint32_t> g_maxDurationMs{60000};
TaskHandle_t g_beepTask = nullptr;
TaskHandle_t g_stopTimerTask = nullptr;

void fillSine(int16_t* out, const size_t count, const float hz, float& phase) {
  const float phaseInc = (hz * 2.0f * static_cast<float>(M_PI)) / static_cast<float>(kSampleRate);
  for (size_t i = 0; i < count; ++i) {
    out[i] = static_cast<int16_t>(std::sin(phase) * static_cast<float>(kAmplitude));
    phase += phaseInc;
    if (phase > 2.0f * static_cast<float>(M_PI)) {
      phase -= 2.0f * static_cast<float>(M_PI);
    }
  }
}

void playToneMs(const float hz, const uint32_t durationMs, float& phase) {
  auto buf = static_cast<int16_t*>(malloc(kChunkSamples * sizeof(int16_t)));
  if (buf == nullptr) {
    return;
  }
  const uint32_t totalSamples = (kSampleRate * durationMs) / 1000;
  uint32_t written = 0;
  while (written < totalSamples && !g_stop.load()) {
    const size_t n = std::min(kChunkSamples, static_cast<size_t>(totalSamples - written));
    fillSine(buf, n, hz, phase);
    audio397.playPcm(buf, n);
    written += static_cast<uint32_t>(n);
  }
  free(buf);
}

void beepTask(void*) {
  g_beepPlaying.store(true);
  SETTINGS.applyOutputVolume();
  if (!audio397.begin(kSampleRate)) {
    g_beepPlaying.store(false);
    g_beepTask = nullptr;
    vTaskDelete(nullptr);
    return;
  }

  float phaseA = 0.0f;
  float phaseB = 0.0f;
  uint32_t elapsed = 0;
  const uint32_t maxMs = g_maxDurationMs.load();

  while (!g_stop.load() && elapsed < maxMs) {
    playToneMs(kToneAHz, 180, phaseA);
    if (g_stop.load()) {
      break;
    }
    playToneMs(kToneBHz, 180, phaseB);
    if (g_stop.load()) {
      break;
    }
    vTaskDelay(pdMS_TO_TICKS(80));
    elapsed += 440;
  }

  audio397.shutdown();
  audio397.ensureAmplifierOff();
  g_beepPlaying.store(false);
  g_beepTask = nullptr;
  vTaskDelete(nullptr);
}

void stopTimerTask(void*) {
  const uint32_t waitMs = g_maxDurationMs.load();
  vTaskDelay(pdMS_TO_TICKS(waitMs));
  if (!g_stop.load()) {
    audioFilePlayer.requestStop();
  }
  g_stopTimerTask = nullptr;
  vTaskDelete(nullptr);
}

uint16_t clampDurationSec(const uint16_t durationSec) {
  if (durationSec < 5) {
    return 5;
  }
  if (durationSec > 600) {
    return 600;
  }
  return durationSec;
}

}  // namespace

void alarmPlaybackStart(const char* musicLeaf, const uint16_t durationSec) {
  alarmPlaybackStop();
  g_stop.store(false);
  const uint16_t sec = clampDurationSec(durationSec);
  g_maxDurationMs.store(static_cast<uint32_t>(sec) * 1000U);

  if (musicLeaf != nullptr && musicLeaf[0] != '\0') {
    std::string full = "/music/";
    full += musicLeaf;
    SETTINGS.applyOutputVolume();
    audioFilePlayer.playFile(full);
    if (xTaskCreate(stopTimerTask, "alarmStop", 3072, nullptr, 4, &g_stopTimerTask) != pdPASS) {
      LOG_ERR("ALARM", "Failed to start alarm stop timer");
      g_stopTimerTask = nullptr;
    }
    return;
  }

  if (xTaskCreate(beepTask, "alarmSnd", 4096, nullptr, 5, &g_beepTask) != pdPASS) {
    LOG_ERR("ALARM", "Failed to start alarm beep task");
    g_beepTask = nullptr;
  }
}

void alarmPlaybackStop() {
  g_stop.store(true);
  audioFilePlayer.requestStop();
  if (g_beepTask != nullptr) {
    for (int i = 0; i < 50 && g_beepPlaying.load(); ++i) {
      vTaskDelay(pdMS_TO_TICKS(20));
    }
    g_beepTask = nullptr;
  }
  g_beepPlaying.store(false);
  if (g_stopTimerTask != nullptr) {
    vTaskDelay(pdMS_TO_TICKS(50));
    g_stopTimerTask = nullptr;
  }
}

bool alarmPlaybackIsActive() {
  return g_beepPlaying.load() || audioFilePlayer.isActive();
}

#endif
