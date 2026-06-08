#pragma once
#include <functional>
#include <string>
#include <vector>

#include "./FileBrowserActivity.h"
#include "activities/Activity.h"
#include "components/UITheme.h"
#include "util/ButtonNavigator.h"

struct RecentBook;
struct Rect;

class HomeActivity final : public Activity {
  enum class HomeMenuAction {
    ContinueReading,
    BrowseFiles,
    RecentBooks,
    Pictures,
    Music,
    VoiceRecorder,
    Calendar,
    Clock,
    OpdsBrowser,
    FileTransfer,
    Settings,
  };

  struct HomeMenuEntry {
    HomeMenuAction action;
    std::string label;
    UIIcon icon;
  };

  ButtonNavigator buttonNavigator;
  int selectorIndex = 0;
  bool recentsLoading = false;
  bool recentsLoaded = false;
  bool firstRenderDone = false;
  bool hasOpdsServers = false;
  bool coverRendered = false;
  bool coverBufferStored = false;
  uint8_t* coverBuffer = nullptr;
  std::vector<RecentBook> recentBooks;
  std::vector<HomeMenuEntry> menuEntries;

  void onSelectBook(const std::string& path);
  void onFileBrowserOpen();
  void onRecentsOpen();
  void onSettingsOpen();
  void onFileTransferOpen();
  void onOpdsBrowserOpen();
#if defined(BOARD_ESP32_S3_EPAPER_397)
  void onPicturesOpen();
  void onMusicPlayerOpen();
  void onVoiceRecorderOpen();
  void onCalendarOpen();
  void onClockOpen();
#endif

  int getMenuItemCount() const;
  int getRecentCoverSlotCount() const;
  int getMenuOnlyCount() const;
  int getMenuRowIndex() const;
  void rebuildMenuEntries();
  void runMenuAction(HomeMenuAction action);
  void handleMenuConfirm(int menuRow);
  bool storeCoverBuffer();    // Store frame buffer for cover image
  bool restoreCoverBuffer();  // Restore frame buffer from stored cover
  void freeCoverBuffer();     // Free the stored cover buffer
  void loadRecentBooks(int maxBooks);
  void loadRecentCovers(int coverHeight);

 public:
  explicit HomeActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("Home", renderer, mappedInput) {}
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
};
