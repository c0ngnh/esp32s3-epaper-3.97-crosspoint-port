#pragma once

#include <cstdint>

#if defined(BOARD_ESP32_S3_EPAPER_397)

// musicLeaf: filename under /music (e.g. "song.mp3"). nullptr or empty -> built-in beep.
void alarmPlaybackStart(const char* musicLeaf, uint16_t durationSec);
void alarmPlaybackStop();
bool alarmPlaybackIsActive();

// Legacy names used by a few call sites.
inline void alarmSoundStart() { alarmPlaybackStart(nullptr, 60); }
inline void alarmSoundStop() { alarmPlaybackStop(); }
inline bool alarmSoundIsPlaying() { return alarmPlaybackIsActive(); }

#endif
