#include "WavUtil.h"

#include <FsHelpers.h>
#include <HalStorage.h>
#include <algorithm>
#include <cstring>

namespace {

bool readFourCC(HalFile& file, char out[4]) {
  return file.read(reinterpret_cast<uint8_t*>(out), 4) == 4;
}

uint32_t readBe32(const uint8_t* p) {
  return (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16) |
         (static_cast<uint32_t>(p[2]) << 8) | static_cast<uint32_t>(p[3]);
}

size_t id3v2SkipSize(HalFile& file) {
  file.seek(0);
  uint8_t hdr[10];
  if (file.read(hdr, 10) != 10) {
    return 0;
  }
  if (hdr[0] != 'I' || hdr[1] != 'D' || hdr[2] != '3') {
    return 0;
  }
  const uint32_t tagSize = ((hdr[6] & 0x7Fu) << 21) | ((hdr[7] & 0x7Fu) << 14) | ((hdr[8] & 0x7Fu) << 7) |
                           static_cast<uint32_t>(hdr[9] & 0x7Fu);
  return 10u + tagSize;
}

struct MpegAudioParams {
  uint32_t sampleRate = 0;
  uint32_t bitrateKbps = 0;
  uint32_t samplesPerFrame = 0;
  bool valid = false;
};

bool parseMpegFrameHeader(const uint32_t h, MpegAudioParams& out) {
  out = {};
  if ((h & 0xFFE00000u) != 0xFFE00000u) {
    return false;
  }
  const uint8_t versionBits = static_cast<uint8_t>((h >> 19) & 3u);
  const uint8_t layerBits = static_cast<uint8_t>((h >> 17) & 3u);
  if (layerBits != 1u) {
    return false;
  }
  const uint8_t srIndex = static_cast<uint8_t>((h >> 10) & 3u);
  if (srIndex == 3u) {
    return false;
  }
  const uint8_t brIndex = static_cast<uint8_t>((h >> 12) & 0xFu);
  if (brIndex == 0 || brIndex == 0xF) {
    return false;
  }

  static const uint32_t kSrMpeg1[] = {44100, 48000, 32000};
  static const uint32_t kSrMpeg2[] = {22050, 24000, 16000};
  static const uint32_t kSrMpeg25[] = {11025, 12000, 8000};
  static const uint16_t kBrMpeg1L3[] = {0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320};
  static const uint16_t kBrMpeg2L3[] = {0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160};

  if (versionBits == 3u) {
    out.sampleRate = kSrMpeg1[srIndex];
    out.samplesPerFrame = 1152u;
    out.bitrateKbps = kBrMpeg1L3[brIndex];
  } else if (versionBits == 2u) {
    out.sampleRate = kSrMpeg2[srIndex];
    out.samplesPerFrame = 576u;
    out.bitrateKbps = kBrMpeg2L3[brIndex];
  } else if (versionBits == 0u) {
    out.sampleRate = kSrMpeg25[srIndex];
    out.samplesPerFrame = 576u;
    out.bitrateKbps = kBrMpeg2L3[brIndex];
  } else {
    return false;
  }
  out.valid = out.sampleRate > 0 && out.bitrateKbps > 0 && out.samplesPerFrame > 0;
  return out.valid;
}

bool findFirstMpegHeader(const uint8_t* data, const size_t len, MpegAudioParams& out) {
  for (size_t i = 0; i + 4 < len; ++i) {
    if (data[i] != 0xFF || (data[i + 1] & 0xE0) != 0xE0) {
      continue;
    }
    if (parseMpegFrameHeader(readBe32(data + i), out)) {
      return true;
    }
  }
  return false;
}

bool durationFromFrames(const uint32_t frames, const MpegAudioParams& mpeg, uint32_t& outSec) {
  if (!mpeg.valid || frames == 0) {
    return false;
  }
  const uint64_t samples = static_cast<uint64_t>(frames) * mpeg.samplesPerFrame;
  outSec = static_cast<uint32_t>(samples / mpeg.sampleRate);
  return outSec >= 1;
}

bool durationFromBytes(const uint32_t audioBytes, const MpegAudioParams& mpeg, uint32_t& outSec) {
  if (!mpeg.valid || audioBytes == 0 || mpeg.bitrateKbps == 0) {
    return false;
  }
  const uint64_t bps = static_cast<uint64_t>(mpeg.bitrateKbps) * 1000u;
  outSec = static_cast<uint32_t>((static_cast<uint64_t>(audioBytes) * 8u) / bps);
  return outSec >= 1;
}

bool tryXingTag(const uint8_t* data, const size_t len, const size_t tagAt, const MpegAudioParams& mpeg,
                uint32_t& outSec) {
  if (tagAt + 12 > len) {
    return false;
  }
  const uint32_t flags = readBe32(data + tagAt + 4);
  size_t off = tagAt + 8;
  uint32_t frames = 0;
  uint32_t bytes = 0;
  if (flags & 1u) {
    if (off + 4 > len) {
      return false;
    }
    frames = readBe32(data + off);
    off += 4;
  }
  if (flags & 2u) {
    if (off + 4 > len) {
      return false;
    }
    bytes = readBe32(data + off);
    off += 4;
  }
  if (frames > 0 && durationFromFrames(frames, mpeg, outSec)) {
    return true;
  }
  if (bytes > 0 && durationFromBytes(bytes, mpeg, outSec)) {
    return true;
  }
  return false;
}

bool tryVbriTag(const uint8_t* data, const size_t len, const size_t tagAt, const MpegAudioParams& mpeg,
                uint32_t& outSec) {
  if (tagAt + 18 > len) {
    return false;
  }
  const uint32_t bytes = readBe32(data + tagAt + 10);
  const uint32_t frames = readBe32(data + tagAt + 14);
  if (frames > 0 && durationFromFrames(frames, mpeg, outSec)) {
    return true;
  }
  if (bytes > 0 && durationFromBytes(bytes, mpeg, outSec)) {
    return true;
  }
  return false;
}

bool scanVbrTags(const uint8_t* data, const size_t len, const MpegAudioParams& mpeg, uint32_t& outSec) {
  for (size_t i = 0; i + 8 < len; ++i) {
    if (memcmp(data + i, "Xing", 4) == 0 || memcmp(data + i, "Info", 4) == 0) {
      if (tryXingTag(data, len, i, mpeg, outSec)) {
        return true;
      }
    }
    if (memcmp(data + i, "VBRI", 4) == 0) {
      if (tryVbriTag(data, len, i, mpeg, outSec)) {
        return true;
      }
    }
  }
  return false;
}

bool probeMp3Duration(HalFile& file, const size_t fileSize, uint32_t& outSec) {
  // Keep small: this runs on the activity task stack (Xing/VBRI live in the first few KB).
  constexpr size_t kScanMax = 4096;
  uint8_t buf[kScanMax];

  const size_t id3 = id3v2SkipSize(file);
  file.seek(id3);
  const size_t headLen = std::min(kScanMax, fileSize > id3 ? fileSize - id3 : 0u);
  const int headRead = file.read(buf, headLen);
  if (headRead < 128) {
    return false;
  }

  MpegAudioParams mpeg{};
  if (!findFirstMpegHeader(buf, static_cast<size_t>(headRead), mpeg)) {
    return false;
  }

  if (scanVbrTags(buf, static_cast<size_t>(headRead), mpeg, outSec)) {
    return true;
  }

  if (fileSize > id3 + headLen + 512) {
    const size_t tailStart = fileSize > kScanMax ? fileSize - kScanMax : id3;
    file.seek(tailStart);
    const size_t tailLen = std::min(kScanMax, fileSize - tailStart);
    const int tailRead = file.read(buf, tailLen);
    if (tailRead >= 128 && scanVbrTags(buf, static_cast<size_t>(tailRead), mpeg, outSec)) {
      return true;
    }
  }

  const size_t audioBytes = fileSize > id3 ? fileSize - id3 : fileSize;
  const uint32_t audioBytes32 = static_cast<uint32_t>(std::min(audioBytes, static_cast<size_t>(UINT32_MAX)));
  if (durationFromBytes(audioBytes32, mpeg, outSec)) {
    return true;
  }

  constexpr uint32_t kFallbackBps = 192000u;
  outSec = static_cast<uint32_t>((fileSize * 8u) / kFallbackBps);
  return outSec >= 1;
}

}  // namespace

bool parseWavHeader(HalFile& file, WavInfo& out) {
  out = {};
  if (!file) {
    return false;
  }
  file.seek(0);

  char riff[4];
  if (!readFourCC(file, riff) || memcmp(riff, "RIFF", 4) != 0) {
    return false;
  }

  uint32_t chunkSize = 0;
  if (file.read(reinterpret_cast<uint8_t*>(&chunkSize), 4) != 4) {
    return false;
  }

  char wave[4];
  if (!readFourCC(file, wave) || memcmp(wave, "WAVE", 4) != 0) {
    return false;
  }

  bool gotFmt = false;
  while (file.available()) {
    char id[4];
    if (!readFourCC(file, id)) {
      break;
    }
    uint32_t subSize = 0;
    if (file.read(reinterpret_cast<uint8_t*>(&subSize), 4) != 4) {
      break;
    }

    if (memcmp(id, "fmt ", 4) == 0 && subSize >= 16) {
      uint16_t audioFormat = 0;
      file.read(reinterpret_cast<uint8_t*>(&audioFormat), 2);
      file.read(reinterpret_cast<uint8_t*>(&out.channels), 2);
      file.read(reinterpret_cast<uint8_t*>(&out.sampleRate), 4);
      uint32_t byteRate = 0;
      file.read(reinterpret_cast<uint8_t*>(&byteRate), 4);
      uint16_t blockAlign = 0;
      file.read(reinterpret_cast<uint8_t*>(&blockAlign), 2);
      file.read(reinterpret_cast<uint8_t*>(&out.bitsPerSample), 2);
      if (subSize > 16) {
        file.seek(file.position() + (subSize - 16));
      }
      gotFmt = audioFormat == 1;
    } else if (memcmp(id, "data", 4) == 0) {
      out.dataOffset = file.position();
      out.dataSize = subSize;
      out.valid = gotFmt && out.sampleRate > 0 && out.bitsPerSample == 16;
      return out.valid;
    } else {
      file.seek(file.position() + subSize);
    }
  }
  return false;
}

void writeWavHeader(HalFile& file, const uint32_t sampleRate, const uint16_t channels, const uint16_t bitsPerSample,
                    const uint32_t dataBytes) {
  const uint32_t byteRate = sampleRate * channels * bitsPerSample / 8;
  const uint16_t blockAlign = channels * bitsPerSample / 8;
  const uint32_t riffSize = 36 + dataBytes;

  file.seek(0);
  file.write(reinterpret_cast<const uint8_t*>("RIFF"), 4);
  file.write(reinterpret_cast<const uint8_t*>(&riffSize), 4);
  file.write(reinterpret_cast<const uint8_t*>("WAVE"), 4);
  file.write(reinterpret_cast<const uint8_t*>("fmt "), 4);
  const uint32_t fmtSize = 16;
  file.write(reinterpret_cast<const uint8_t*>(&fmtSize), 4);
  const uint16_t audioFormat = 1;
  file.write(reinterpret_cast<const uint8_t*>(&audioFormat), 2);
  file.write(reinterpret_cast<const uint8_t*>(&channels), 2);
  file.write(reinterpret_cast<const uint8_t*>(&sampleRate), 4);
  file.write(reinterpret_cast<const uint8_t*>(&byteRate), 4);
  file.write(reinterpret_cast<const uint8_t*>(&blockAlign), 2);
  file.write(reinterpret_cast<const uint8_t*>(&bitsPerSample), 2);
  file.write(reinterpret_cast<const uint8_t*>("data"), 4);
  file.write(reinterpret_cast<const uint8_t*>(&dataBytes), 4);
}

void finalizeWavHeader(HalFile& file, const uint32_t dataBytes) { writeWavHeader(file, 24000, 1, 16, dataBytes); }

bool isMp3Path(const std::string& path) {
  return FsHelpers::checkFileExtension(path, ".mp3");
}

bool isWavPath(const std::string& path) {
  return FsHelpers::checkFileExtension(path, ".wav");
}

bool probeTrackDurationSec(const std::string& path, uint32_t& outDurationSec) {
  outDurationSec = 0;
  if (isWavPath(path)) {
    HalFile file;
    if (!Storage.openFileForRead("WAV", path, file)) {
      return false;
    }
    WavInfo info{};
    if (!parseWavHeader(file, info) || !info.valid) {
      return false;
    }
    const uint32_t bytesPerSec =
        info.sampleRate * static_cast<uint32_t>(info.channels) * (info.bitsPerSample / 8u);
    if (bytesPerSec == 0) {
      return false;
    }
    outDurationSec = info.dataSize / bytesPerSec;
    return outDurationSec > 0;
  }
  if (isMp3Path(path)) {
    HalFile file;
    if (!Storage.openFileForRead("MP3", path, file)) {
      return false;
    }
    const size_t fileSize = file.size();
    if (fileSize < 1024) {
      return false;
    }
    return probeMp3Duration(file, fileSize, outDurationSec);
  }
  return false;
}
