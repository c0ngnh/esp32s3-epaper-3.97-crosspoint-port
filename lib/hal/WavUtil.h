#pragma once

#include <cstdint>
#include <string>

class HalFile;

struct WavInfo {
  uint32_t sampleRate = 0;
  uint16_t channels = 0;
  uint16_t bitsPerSample = 0;
  uint32_t dataOffset = 0;
  uint32_t dataSize = 0;
  bool valid = false;
};

bool parseWavHeader(HalFile& file, WavInfo& out);

void writeWavHeader(HalFile& file, uint32_t sampleRate, uint16_t channels, uint16_t bitsPerSample, uint32_t dataBytes);

void finalizeWavHeader(HalFile& file, uint32_t dataBytes);

bool isMp3Path(const std::string& path);
bool isWavPath(const std::string& path);

// Best-effort track length for UI (WAV exact; MP3 via Xing/Info/VBRI when present).
bool probeTrackDurationSec(const std::string& path, uint32_t& outDurationSec);
