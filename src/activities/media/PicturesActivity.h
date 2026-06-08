#pragma once

#if defined(BOARD_ESP32_S3_EPAPER_397)

#include <cstddef>
#include <string>
#include <vector>

#include "activities/Activity.h"
#include "components/themes/BaseTheme.h"
#include "util/ButtonNavigator.h"

class PicturesActivity final : public Activity {
  ButtonNavigator buttonNavigator;
  std::vector<std::string> images;
  std::string previewPath;
  size_t selectorIndex = 0;

  void loadImages();
  void syncPreviewFromSelection();
  void drawPreviewTile(const Rect& tile) const;

 public:
  explicit PicturesActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("Pictures", renderer, mappedInput) {}
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
};

#endif
