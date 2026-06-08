#include "AudioFilePlayer.h"

#if defined(BOARD_ESP32_S3_EPAPER_397)

#include <HalAudio397.h>
#include <HalStorage.h>
#include <Logging.h>
#include <MP3DecoderHelix.h>
#include <Memory.h>

#include <algorithm>
#include <cstring>

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include "WavUtil.h"

using libhelix::MP3DecoderHelix;

AudioFilePlayer audioFilePlayer;

namespace {

struct PlayTaskCtx {
  std::string path;
};

struct Mp3PlayCtx {
  AudioFilePlayer* player = nullptr;
  int16_t* monoBuf = nullptr;
  size_t monoCap = 0;
};

Mp3PlayCtx* g_mp3Ctx = nullptr;

SemaphoreHandle_t playDoneSem = nullptr;
uint32_t mp3OutputRate = 0;

constexpr size_t PCM_CHUNK_SAMPLES = 512;
constexpr TickType_t PLAY_STOP_WAIT_MS = 8000;
constexpr size_t MP3_INPUT_BUF = 1024;

void downmixToMono(const int16_t* in, size_t frameSamples, int channels, int16_t* out) {
  if (channels <= 1) {
    memcpy(out, in, frameSamples * sizeof(int16_t));
    return;
  }
  for (size_t i = 0; i < frameSamples; ++i) {
    const int32_t l = in[i * 2];
    const int32_t r = in[i * 2 + 1];
    out[i] = static_cast<int16_t>((l + r) / 2);
  }
}

void mp3Callback(MP3FrameInfo& info, int16_t* pcmBuffer, size_t len, void*) {
  if (g_mp3Ctx == nullptr || g_mp3Ctx->player == nullptr || pcmBuffer == nullptr || len == 0) {
    return;
  }
  if (g_mp3Ctx->player->shouldStop() || g_mp3Ctx->player->isPaused()) {
    return;
  }
  if (info.samprate > 0) {
    const uint32_t rate = static_cast<uint32_t>(info.samprate);
    if (rate != mp3OutputRate) {
      if (!audio397.begin(rate)) {
        LOG_ERR("AUDIO", "MP3 begin failed @ %lu Hz", static_cast<unsigned long>(rate));
        return;
      }
      mp3OutputRate = rate;
      LOG_INF("AUDIO", "MP3 output @ %lu Hz", static_cast<unsigned long>(rate));
    }
  }
  const int channels = info.nChans > 0 ? info.nChans : 1;
  const size_t frameSamples = len / static_cast<size_t>(channels);
  if (frameSamples == 0 || frameSamples > g_mp3Ctx->monoCap) {
    return;
  }
  downmixToMono(pcmBuffer, frameSamples, channels, g_mp3Ctx->monoBuf);
  audio397.playPcm(g_mp3Ctx->monoBuf, frameSamples);
}

}  // namespace

void AudioFilePlayer::requestStop() {
  _stop.store(true);
  _paused.store(false);
}

void AudioFilePlayer::setPaused(const bool paused) {
  if (_playing.load()) {
    _paused.store(paused);
  }
}

bool AudioFilePlayer::togglePause() {
  if (!_playing.load()) {
    return false;
  }
  _paused.store(!_paused.load());
  return _paused.load();
}

void AudioFilePlayer::stopAndWait() {
  if (!_playing.load()) {
    return;
  }
  requestStop();
  if (playDoneSem == nullptr) {
    playDoneSem = xSemaphoreCreateBinary();
  }
  if (playDoneSem != nullptr) {
    xSemaphoreTake(playDoneSem, 0);
    if (xSemaphoreTake(playDoneSem, pdMS_TO_TICKS(PLAY_STOP_WAIT_MS)) != pdTRUE) {
      LOG_ERR("AUDIO", "Timed out waiting for playback task to stop");
    }
  }
  _playing.store(false);
}

bool AudioFilePlayer::playWav(const std::string& path) {
  HalFile file;
  if (!Storage.openFileForRead("WAV", path, file)) {
    return false;
  }

  WavInfo info{};
  if (!parseWavHeader(file, info)) {
    LOG_ERR("AUDIO", "WAV header invalid: %s", path.c_str());
    return false;
  }

  if (!audio397.begin(info.sampleRate)) {
    LOG_ERR("AUDIO", "WAV begin failed @ %lu Hz: %s", static_cast<unsigned long>(info.sampleRate), path.c_str());
    return false;
  }

  file.seek(info.dataOffset);
  auto pcmBuf = makeUniqueNoThrow<int16_t[]>(PCM_CHUNK_SAMPLES * 2);
  auto monoBuf = makeUniqueNoThrow<int16_t[]>(PCM_CHUNK_SAMPLES);
  if (!pcmBuf || !monoBuf) {
    return false;
  }

  uint32_t remaining = info.dataSize;
  while (remaining > 0 && !_stop.load()) {
    while (_paused.load() && !_stop.load()) {
      vTaskDelay(pdMS_TO_TICKS(20));
    }
    const size_t toRead =
        std::min(remaining, static_cast<uint32_t>(PCM_CHUNK_SAMPLES * sizeof(int16_t)));
    const int n = file.read(reinterpret_cast<uint8_t*>(pcmBuf.get()), toRead);
    if (n <= 0) {
      break;
    }
    remaining -= static_cast<uint32_t>(n);
    const size_t samples = static_cast<size_t>(n) / sizeof(int16_t);
    if (info.channels == 1) {
      audio397.playPcm(pcmBuf.get(), samples);
    } else {
      downmixToMono(pcmBuf.get(), samples / info.channels, info.channels, monoBuf.get());
      audio397.playPcm(monoBuf.get(), samples / info.channels);
    }
  }
  return true;
}

bool AudioFilePlayer::playMp3(const std::string& path) {
  HalFile file;
  if (!Storage.openFileForRead("MP3", path, file)) {
    return false;
  }

  mp3OutputRate = 0;

  auto monoBuf = makeUniqueNoThrow<int16_t[]>(1152);
  if (!monoBuf) {
    return false;
  }

  Mp3PlayCtx ctx{this, monoBuf.get(), 1152};
  g_mp3Ctx = &ctx;

  MP3DecoderHelix decoder(mp3Callback);
  decoder.begin();

  uint8_t input[MP3_INPUT_BUF];
  while (!_stop.load() && file.available()) {
    while (_paused.load() && !_stop.load()) {
      vTaskDelay(pdMS_TO_TICKS(20));
    }
    const int n = file.read(input, sizeof(input));
    if (n <= 0) {
      break;
    }
    decoder.write(input, static_cast<size_t>(n));
  }

  g_mp3Ctx = nullptr;
  return true;
}

void AudioFilePlayer::playTask(void* param) {
  auto* ctx = static_cast<PlayTaskCtx*>(param);
  if (ctx == nullptr) {
    vTaskDelete(nullptr);
    return;
  }

  audioFilePlayer._playing.store(true);
  audioFilePlayer._stop.store(false);
  audioFilePlayer._paused.store(false);

  // playWav/playMp3 call begin()/shutdown() which take the audio mutex — do not lock here.
  bool ok = false;
  if (isWavPath(ctx->path)) {
    ok = audioFilePlayer.playWav(ctx->path);
  } else if (isMp3Path(ctx->path)) {
    ok = audioFilePlayer.playMp3(ctx->path);
  } else {
    LOG_ERR("AUDIO", "Unsupported format: %s", ctx->path.c_str());
  }

  if (!audioFilePlayer._keepCodecWarm.load()) {
    audio397.shutdown();
  }

  if (!ok) {
    LOG_ERR("AUDIO", "Playback failed: %s", ctx->path.c_str());
  }

  delete ctx;
  audioFilePlayer._playing.store(false);
  audioFilePlayer._paused.store(false);
  if (playDoneSem != nullptr) {
    xSemaphoreGive(playDoneSem);
  }
  vTaskDelete(nullptr);
}

bool AudioFilePlayer::playFile(const std::string& path) {
  stopAndWait();
  _stop.store(false);

  if (playDoneSem == nullptr) {
    playDoneSem = xSemaphoreCreateBinary();
  }

  auto* ctx = new (std::nothrow) PlayTaskCtx{path};
  if (ctx == nullptr) {
    return false;
  }

  TaskHandle_t handle = nullptr;
  if (xTaskCreate(playTask, "audioPlay", 8192, ctx, 5, &handle) != pdPASS) {
    delete ctx;
    return false;
  }
  return true;
}

#endif
