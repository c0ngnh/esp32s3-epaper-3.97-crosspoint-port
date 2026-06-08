#pragma once

#if defined(BOARD_ESP32_S3_EPAPER_397)

#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

#include "activities/Activity.h"
#include "components/themes/BaseTheme.h"
#include "util/ButtonNavigator.h"

class VoiceRecorderActivity final : public Activity {
 public:
  VoiceRecorderActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool preventAutoSleep() override;
  bool usesAppGestures() const override { return true; }

 private:
  ButtonNavigator buttonNavigator;
  std::vector<std::string> recordings;
  size_t selectorIndex = 0;
  std::string recordDir = "/recordings";
  std::string nowPlaying;
  std::string statusMessage;
  bool recording = false;
  bool bigTileFocused = true;
  unsigned long playStartMs = 0;
  unsigned long recordStartMs = 0;
  uint32_t lastTileDisplaySec = UINT32_MAX;
  unsigned long totalPausedMs = 0;
  unsigned long pauseStartMs = 0;
  uint32_t trackDurationSec = 0;
  std::atomic<bool> stopRecord{false};
  TaskHandle_t recordTaskHandle = nullptr;

  void loadRecordings();
  void startRecording();
  void stopRecording();
  void startPlayback(const std::string& leaf);
  void stopPlayback();
  void handleConfirm();
  void deleteSelected();
  void adjustVolume(int delta);
  void syncPauseClock();
  uint32_t playbackElapsedSec() const;
  uint32_t recordElapsedSec() const;
  void drawRecorderTile(const Rect& tile);
  static void recordTask(void* param);
};

#endif
