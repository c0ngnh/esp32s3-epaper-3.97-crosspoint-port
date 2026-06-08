#pragma once
#include "activities/Activity.h"

class Bitmap;

class SleepActivity final : public Activity {
 public:
  explicit SleepActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, bool shutdownWallpaper = false)
      : Activity("Sleep", renderer, mappedInput), _shutdownWallpaper(shutdownWallpaper) {}
  void onEnter() override;

 private:
  bool _shutdownWallpaper = false;
  void drawSleepStatusIndicator() const;
  void renderDefaultSleepScreen() const;
  void renderCustomSleepScreen() const;
  void renderCoverSleepScreen() const;
  void renderBitmapSleepScreen(const Bitmap& bitmap) const;
  void renderBlankSleepScreen() const;
#if defined(BOARD_ESP32_S3_EPAPER_397)
  bool render397PicturesSleepScreen(bool shutdownWallpaper) const;
  bool renderImageFileSleepScreen(const std::string& path) const;
#endif
};
