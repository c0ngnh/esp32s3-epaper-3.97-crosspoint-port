#pragma once
#include <Logging.h>

#include <cassert>
#include <memory>
#include <string>
#include <utility>

#include "ActivityManager.h"  // for using the ActivityManager singleton
#include "ActivityResult.h"
#include "GfxRenderer.h"
#include "MappedInputManager.h"
#include "RenderLock.h"
#include "util/ScreenshotInfo.h"

class Activity {
  friend class ActivityManager;

 protected:
  std::string name;
  GfxRenderer& renderer;
  MappedInputManager& mappedInput;

  ActivityResultHandler resultHandler;
  ActivityResult result;

#if defined(BOARD_ESP32_S3_EPAPER_397)
  // Center-key OK helper (3.97"); same as wasConfirmClicked().
  bool consumeConfirmClick();
  void armBackGestureLock();
  // Hard-shake on accelerometer opens delete confirmation (systemwide on 397).
  bool consumeShakeDeleteRequest();
#endif

 public:
  explicit Activity(std::string name, GfxRenderer& renderer, MappedInputManager& mappedInput)
      : name(std::move(name)), renderer(renderer), mappedInput(mappedInput) {}
  virtual ~Activity() = default;
  const char* getActivityName() const { return name.c_str(); }
  virtual void onEnter();
  virtual void onExit();
  virtual void loop() {}

  virtual void render(RenderLock&&) {}

  // If immediate is true, the update will be triggered immediately.
  // Otherwise, it will be deferred until the end of the current loop iteration.
  virtual void requestUpdate(bool immediate = false);

  // Request an immediate render and block until it completes.
  virtual void requestUpdateAndWait();

  virtual bool skipLoopDelay() { return false; }
  virtual bool preventAutoSleep() { return false; }
#if defined(BOARD_ESP32_S3_EPAPER_397)
  virtual bool usesAppGestures() const { return false; }
#endif
  virtual bool isReaderActivity() const { return false; }
  /// On 397: replacing ReaderActivity with a non-reader activity normally forces a full refresh
  /// (`requestDisplayResync`). Image viewer should use the same partial refresh as sibling navigation.
  virtual bool skipsReaderReplaceDisplayResync() const { return false; }
  // When false, USB plug/unplug will not queue an extra screen refresh (static viewers).
  virtual bool wantsScreenRefreshOnUsbChange() const { return true; }
  virtual ScreenshotInfo getScreenshotInfo() const { return {}; }

  // Start a new activity without destroying the current one
  // Note: requestUpdate() will be invoked automatically once resultHandler finishes
  void startActivityForResult(std::unique_ptr<Activity>&& activity, ActivityResultHandler resultHandler);

  // Set the result to be passed back to the previous activity when this activity finishes
  void setResult(ActivityResult&& result);

  // Finish this activity and return to the previous one on the stack (if any)
  void finish();

  virtual void onSelectBook(const std::string& path);
};
