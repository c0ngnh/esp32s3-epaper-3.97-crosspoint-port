#pragma once

// Request deep sleep from an activity (handled on the next main loop iteration).
void requestDeepSleep(bool shutdownWallpaper = false);
bool consumeDeepSleepRequest(bool& shutdownWallpaper);
