#pragma once

#include <Arduino.h>

#if defined(BOARD_ESP32_S3_EPAPER_397)

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

// ES8311 + I2S playback/record for Waveshare ESP32-S3 ePaper 3.97.
class HalAudio397 {
 public:
  static constexpr uint32_t DEFAULT_SAMPLE_RATE = 24000;

  bool begin(uint32_t sampleRate = DEFAULT_SAMPLE_RATE);
  void shutdown();

  // Drive NS4150B PA enable low at boot / before codec use (avoids idle pop).
  static void ensureAmplifierOff();

  bool isActive() const { return _active; }
  uint32_t getSampleRate() const { return _sampleRate; }

  bool setOutputVolume(uint8_t percent);
  bool playPcm(const int16_t* data, size_t sampleCount);
  size_t recordPcm(int16_t* buf, size_t maxSamples);

  bool lock(TickType_t timeout = pdMS_TO_TICKS(5000));
  void unlock();

 private:
  bool _active = false;
  uint32_t _sampleRate = DEFAULT_SAMPLE_RATE;
  uint8_t _outputVolume = 70;
  void* _codecHandle = nullptr;
  SemaphoreHandle_t _mutex = nullptr;

  bool initCodec(uint32_t sampleRate);
  bool initI2s(uint32_t sampleRate);
  void setPa(bool on);
  // Caller must hold _mutex.
  void shutdownLocked();
};

extern HalAudio397 audio397;

#endif
