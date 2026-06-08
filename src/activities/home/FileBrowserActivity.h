#pragma once

#include <functional>
#include <string>
#include <vector>

#include "RecentBooksStore.h"
#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

class FileBrowserActivity final : public Activity {
 public:
  // Books = standard reader browser; PickFirmware = filter to .bin only and return path via ActivityResult.
  enum class Mode { Books, PickFirmware };

 private:
  // Deletion
  void clearFileMetadata(const std::string& fullPath);

  ButtonNavigator buttonNavigator;

  size_t selectorIndex = 0;

  bool lockLongPressBack = false;
#if !defined(BOARD_ESP32_S3_EPAPER_397)
  // Swallow the next Confirm release when opened while Confirm was already held.
  bool lockNextConfirmRelease = false;
#endif

  Mode mode = Mode::Books;
  unsigned long gestureSettleUntilMs = 0;

  // Files state
  std::string basepath = "/";
  std::vector<std::string> files;

  // Data loading
  void loadFiles();
  int findEntry(const std::string& name) const;

 public:
  explicit FileBrowserActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string initialPath = "/",
                               Mode mode = Mode::Books)
      : Activity("FileBrowser", renderer, mappedInput),
        mode(mode),
        basepath(initialPath.empty() ? "/" : std::move(initialPath)) {}
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  void onSelectBook(const std::string& path) override;
#if defined(BOARD_ESP32_S3_EPAPER_397)
  bool usesAppGestures() const override { return true; }
#endif
};
