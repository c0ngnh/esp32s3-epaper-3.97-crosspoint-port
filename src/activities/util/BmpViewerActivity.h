#pragma once

#include <functional>
#include <string>

#include "MappedInputManager.h"
#include "activities/Activity.h"
#include <GfxRenderer.h>

class BmpViewerActivity final : public Activity {
 public:
  BmpViewerActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string filePath);

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool preventAutoSleep() override { return true; }
  bool skipsReaderReplaceDisplayResync() const override { return true; }
  bool wantsScreenRefreshOnUsbChange() const override { return false; }

 private:
  void loadSiblingImages();
  bool getImageDimensions(int& width, int& height);
  bool renderImage();
  void reloadView();
  void doSetSleepCover();
  void doDeleteImage();

  std::string filePath;
  std::vector<std::string> siblingImages;
  int currentImageIndex = -1;
  GfxRenderer::Orientation entryOrientation = GfxRenderer::Portrait;
  std::string displayedPath;
  bool screenUpToDate = false;
};