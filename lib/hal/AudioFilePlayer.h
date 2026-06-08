#pragma once

#include <atomic>
#include <string>

#if defined(BOARD_ESP32_S3_EPAPER_397)

class AudioFilePlayer {
 public:
  bool playFile(const std::string& path);
  // When true, the codec stays up between playFile() calls (faster start, gapless playlist).
  void setKeepCodecWarm(bool warm) { _keepCodecWarm.store(warm); }
  void requestStop();
  // Blocks until the play task exits (call before shutdown or starting another file).
  void stopAndWait();
  bool isPlaying() const { return _playing.load() && !_paused.load(); }
  bool isPaused() const { return _playing.load() && _paused.load(); }
  bool isActive() const { return _playing.load(); }
  bool shouldStop() const { return _stop.load(); }
  void setPaused(bool paused);
  bool togglePause();

 private:
  std::atomic<bool> _stop{false};
  std::atomic<bool> _playing{false};
  std::atomic<bool> _paused{false};
  std::atomic<bool> _keepCodecWarm{false};

  static void playTask(void* param);
  bool playWav(const std::string& path);
  bool playMp3(const std::string& path);
};

extern AudioFilePlayer audioFilePlayer;

#endif
