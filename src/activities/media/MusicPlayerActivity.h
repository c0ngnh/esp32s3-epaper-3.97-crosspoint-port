#pragma once



#if defined(BOARD_ESP32_S3_EPAPER_397)



#include <cstdint>
#include <string>

#include <vector>



#include "activities/Activity.h"
#include "components/themes/BaseTheme.h"
#include "util/ButtonNavigator.h"



class MusicPlayerActivity final : public Activity {

 public:

  MusicPlayerActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);



  void onEnter() override;

  void onExit() override;

  void loop() override;

  void render(RenderLock&&) override;

  bool preventAutoSleep() override;
  bool usesAppGestures() const override { return true; }



 private:

  ButtonNavigator buttonNavigator;

  std::vector<std::string> tracks;

  size_t selectorIndex = 0;
  unsigned long playStartMs = 0;
  unsigned long totalPausedMs = 0;
  uint32_t lastPlaybackDisplaySec = UINT32_MAX;
  bool fullRenderNeeded = true;
  unsigned long pauseStartMs = 0;
  uint32_t trackDurationSec = 0;

  std::string nowPlaying;

  std::string musicDir = "/music";

  void loadTracks();

  void startPlayback(const std::string& path);

  void stopPlayback();

  void playRandomTrack();

  void adjustVolume(int delta);
  void syncPauseClock();
  uint32_t playbackElapsedSec() const;
  void drawPlayerTile(const Rect& tile, const std::string& title, bool playbackActive) const;
  void patchPlaybackFooter(const Rect& tile) const;
  void markFullRenderNeeded();

  void loopPlaybackControls();

  void loopListNavigation();

  void deleteSelected();

};



#endif

