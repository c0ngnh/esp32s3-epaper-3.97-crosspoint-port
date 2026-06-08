#include "Activity.h"

#include "ActivityManager.h"
#if defined(BOARD_ESP32_S3_EPAPER_397)
#include <HalTiltSensor.h>

#include "HalGPIO.h"
#endif

void Activity::onEnter() {
  LOG_DBG("ACT", "Entering activity: %s", name.c_str());
#if defined(BOARD_ESP32_S3_EPAPER_397)
  gpio.discardNavigationConfirm();
  halTiltSensor.clearPendingAppGestures();
  if (usesAppGestures()) {
    halTiltSensor.acquireAppGestures();
  }
#endif
}

#if defined(BOARD_ESP32_S3_EPAPER_397)
bool Activity::consumeConfirmClick() { return mappedInput.wasConfirmClicked(); }

void Activity::armBackGestureLock() {
  gpio.clearPendingConfirmTap();
  gpio.suppressConfirmAfterGesture();
}

bool Activity::consumeShakeDeleteRequest() { return halTiltSensor.consumeShakeDelete(); }
#endif

void Activity::onExit() {
#if defined(BOARD_ESP32_S3_EPAPER_397)
  if (usesAppGestures()) {
    halTiltSensor.releaseAppGestures();
  }
#endif
  LOG_DBG("ACT", "Exiting activity: %s", name.c_str());
}

void Activity::requestUpdate(bool immediate) { activityManager.requestUpdate(immediate); }

void Activity::requestUpdateAndWait() { activityManager.requestUpdateAndWait(); }

void Activity::onSelectBook(const std::string& path) { activityManager.goToReader(path); }

void Activity::startActivityForResult(std::unique_ptr<Activity>&& activity, ActivityResultHandler resultHandler) {
  this->resultHandler = std::move(resultHandler);
  activityManager.pushActivity(std::move(activity));
}

void Activity::setResult(ActivityResult&& result) { this->result = std::move(result); }

void Activity::finish() { activityManager.popActivity(); }
