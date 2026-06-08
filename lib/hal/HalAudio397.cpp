#include "HalAudio397.h"



#if defined(BOARD_ESP32_S3_EPAPER_397)



#include <ESP_I2S.h>

#include <HalBoard397.h>

#include <Logging.h>

#include <es8311.h>  // from lib/audio397 (see library.json srcFilter)



#include <algorithm>



#include "HalGPIO.h"



HalAudio397 audio397;



namespace {

struct ScopedI2cBusLock {
  bool held = false;
  ScopedI2cBusLock() { held = board397.acquireSharedI2c(100); }
  ~ScopedI2cBusLock() {
    if (held) {
      board397.releaseSharedI2c();
    }
  }
};

constexpr uint32_t MCLK_MULTIPLE = 256;

constexpr uint32_t POP_SETTLE_MS = 15;



I2SClass i2s;



void setPaPin(const bool on) {

  pinMode(AUDIO_CTRL_PIN, OUTPUT);

  digitalWrite(AUDIO_CTRL_PIN, on ? HIGH : LOW);

}



void muteCodec(es8311_handle_t handle, const bool mute) {

  if (handle != nullptr) {

    es8311_voice_mute(handle, mute);

  }

}



void flushSilence(const size_t sampleCount = 384) {

  static int16_t zeros[512] = {};

  const size_t n = std::min(sampleCount, sizeof(zeros) / sizeof(zeros[0]));

  i2s.write(reinterpret_cast<const uint8_t*>(zeros), n * sizeof(int16_t));

}



}  // namespace



void HalAudio397::ensureAmplifierOff() { setPaPin(false); }



bool HalAudio397::lock(const TickType_t timeout) {

  if (_mutex == nullptr) {

    return true;

  }

  return xSemaphoreTake(_mutex, timeout) == pdTRUE;

}



void HalAudio397::unlock() {

  if (_mutex != nullptr) {

    xSemaphoreGive(_mutex);

  }

}



void HalAudio397::setPa(const bool on) { setPaPin(on); }



bool HalAudio397::initI2s(const uint32_t sampleRate) {

  i2s.setPins(AUDIO_I2S_SCLK, AUDIO_I2S_LRCK, AUDIO_I2S_DSOUT, AUDIO_I2S_DSDIN, AUDIO_I2S_MCLK);

  if (!i2s.begin(I2S_MODE_STD, sampleRate, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO, I2S_STD_SLOT_LEFT)) {

    LOG_ERR("AUDIO397", "I2S begin failed");

    return false;

  }

  return true;

}



bool HalAudio397::initCodec(const uint32_t sampleRate) {

  board397.ensureWire();
  const ScopedI2cBusLock i2c;
  if (!i2c.held) {
    LOG_ERR("AUDIO397", "I2C bus busy for codec init");
    return false;
  }

  if (_codecHandle != nullptr) {

    es8311_delete(static_cast<es8311_handle_t>(_codecHandle));

    _codecHandle = nullptr;

  }



  es8311_handle_t handle = es8311_create(I2C_NUM_0, I2C_ADDR_ES8311);

  if (handle == nullptr) {

    LOG_ERR("AUDIO397", "es8311_create failed");

    return false;

  }



  const es8311_clock_config_t clk = {

      .mclk_inverted = false,

      .sclk_inverted = false,

      .mclk_from_mclk_pin = true,

      .mclk_frequency = static_cast<int>(sampleRate * MCLK_MULTIPLE),

      .sample_frequency = static_cast<int>(sampleRate),

  };



  if (es8311_init(handle, &clk, ES8311_RESOLUTION_16, ES8311_RESOLUTION_16) != ESP_OK) {

    LOG_ERR("AUDIO397", "es8311_init failed");

    es8311_delete(handle);

    return false;

  }



  es8311_voice_fade(handle, ES8311_FADE_256LRCK);



  int volumeSet = 0;

  es8311_voice_volume_set(handle, _outputVolume, &volumeSet);

  es8311_microphone_config(handle, false);

  // Stay muted until I2S is running and PA is enabled (see begin()).

  muteCodec(handle, true);



  _codecHandle = handle;

  return true;

}



bool HalAudio397::begin(const uint32_t sampleRate) {

  if (_mutex == nullptr) {

    _mutex = xSemaphoreCreateMutex();

  }

  if (!lock()) {

    return false;

  }



  if (_active && _sampleRate == sampleRate) {

    unlock();

    return true;

  }



  shutdownLocked();



  if (!initCodec(sampleRate) || !initI2s(sampleRate)) {

    shutdownLocked();

    unlock();

    return false;

  }



  flushSilence();

  delay(POP_SETTLE_MS);



  muteCodec(static_cast<es8311_handle_t>(_codecHandle), false);

  delay(5);

  setPa(true);



  _sampleRate = sampleRate;

  _active = true;

  LOG_INF("AUDIO397", "Audio ready @ %lu Hz", static_cast<unsigned long>(sampleRate));

  unlock();

  return true;

}



void HalAudio397::shutdownLocked() {

  if (_codecHandle != nullptr) {

    auto* handle = static_cast<es8311_handle_t>(_codecHandle);
    const ScopedI2cBusLock i2c;
    if (i2c.held) {
      muteCodec(handle, true);
      es8311_voice_fade(handle, ES8311_FADE_256LRCK);
    }
    delay(POP_SETTLE_MS);

  }



  if (_active) {

    i2s.end();

  }



  setPa(false);

  delay(5);



  if (_codecHandle != nullptr) {
    const ScopedI2cBusLock i2c;
    (void)i2c;
    es8311_delete(static_cast<es8311_handle_t>(_codecHandle));

    _codecHandle = nullptr;

  }



  _active = false;

}



void HalAudio397::shutdown() {

  if (!lock(pdMS_TO_TICKS(2000))) {

    return;

  }

  shutdownLocked();

  unlock();

}



bool HalAudio397::setOutputVolume(const uint8_t percent) {

  _outputVolume = percent;

  if (!_active || _codecHandle == nullptr) {

    return true;

  }

  if (!lock()) {
    return false;
  }
  const ScopedI2cBusLock i2c;
  int volumeSet = 0;
  const bool ok =
      i2c.held && es8311_voice_volume_set(static_cast<es8311_handle_t>(_codecHandle), percent, &volumeSet) == ESP_OK;
  unlock();
  return ok;

}



bool HalAudio397::playPcm(const int16_t* data, const size_t sampleCount) {

  if (!_active || data == nullptr || sampleCount == 0) {

    return false;

  }

  if (!lock()) {
    return false;
  }
  const size_t bytes = sampleCount * sizeof(int16_t);
  const bool ok = i2s.write(reinterpret_cast<const uint8_t*>(data), bytes) == bytes;
  unlock();
  return ok;

}



size_t HalAudio397::recordPcm(int16_t* buf, const size_t maxSamples) {

  if (!_active || buf == nullptr || maxSamples == 0) {

    return 0;

  }

  if (!lock()) {
    return 0;
  }
  const size_t bytesWanted = maxSamples * sizeof(int16_t);
  const size_t bytesRead = i2s.readBytes(reinterpret_cast<char*>(buf), bytesWanted);
  unlock();
  return bytesRead / sizeof(int16_t);

}



#endif


