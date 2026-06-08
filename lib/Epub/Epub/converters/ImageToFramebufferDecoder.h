#pragma once
#include <HalStorage.h>

#include <memory>
#include <string>

class GfxRenderer;

struct ImageDimensions {
  int16_t width;
  int16_t height;
};

struct RenderConfig {
  int x, y;
  int maxWidth, maxHeight;
  bool useGrayscale = true;
  bool useDithering = true;
  bool performanceMode = false;
  bool useExactDimensions = false;  // If true, use maxWidth/maxHeight as exact output size (no recalculation)
  std::string cachePath;            // If non-empty, decoder will write pixel cache to this path
  // Optional destination clip (logical screen coords). clipW/clipH > 0 enables clipping.
  int clipX = 0;
  int clipY = 0;
  int clipW = 0;
  int clipH = 0;
};

inline void renderConfigSetClipFromRect(RenderConfig& config, const int x, const int y, const int w, const int h) {
  config.clipX = x;
  config.clipY = y;
  config.clipW = w;
  config.clipH = h;
}

inline bool renderRowYInBounds(const RenderConfig& config, const int outY, const int screenHeight) {
  if (outY < 0 || outY >= screenHeight) {
    return false;
  }
  if (config.clipW > 0 && config.clipH > 0) {
    return outY >= config.clipY && outY < config.clipY + config.clipH;
  }
  return true;
}

inline bool renderPixelInBounds(const RenderConfig& config, const int outX, const int outY, const int screenWidth,
                                const int screenHeight) {
  if (outX < 0 || outY < 0 || outX >= screenWidth || outY >= screenHeight) {
    return false;
  }
  if (config.clipW > 0 && config.clipH > 0) {
    return outX >= config.clipX && outX < config.clipX + config.clipW && outY >= config.clipY &&
           outY < config.clipY + config.clipH;
  }
  return true;
}

// Destination column range [start, end) for PNG/JPEG row loops (relative to config.x).
inline void renderDstColumnRange(const RenderConfig& config, const int outXBase, const int dstWidth,
                                 const int screenWidth, int& dstXStart, int& dstXEnd) {
  dstXStart = 0;
  dstXEnd = dstWidth;
  if (outXBase >= screenWidth || outXBase + dstWidth <= 0) {
    dstXEnd = 0;
    return;
  }
  if (outXBase < 0) {
    dstXStart = -outXBase;
  }
  if (outXBase + dstWidth > screenWidth) {
    dstXEnd = screenWidth - outXBase;
  }
  if (config.clipW > 0 && config.clipH > 0) {
    const int clipL = config.clipX - outXBase;
    const int clipR = config.clipX + config.clipW - outXBase;
    if (dstXStart < clipL) {
      dstXStart = clipL;
    }
    if (dstXEnd > clipR) {
      dstXEnd = clipR;
    }
  }
  if (dstXStart < 0) {
    dstXStart = 0;
  }
  if (dstXEnd > dstWidth) {
    dstXEnd = dstWidth;
  }
  if (dstXStart > dstXEnd) {
    dstXEnd = dstXStart;
  }
}

class ImageToFramebufferDecoder {
 public:
  virtual ~ImageToFramebufferDecoder() = default;

  virtual bool decodeToFramebuffer(const std::string& imagePath, GfxRenderer& renderer, const RenderConfig& config) = 0;

  virtual bool getDimensions(const std::string& imagePath, ImageDimensions& dims) const = 0;

  virtual const char* getFormatName() const = 0;

 protected:
  // Size validation helpers
  static constexpr int MAX_SOURCE_PIXELS = 3145728;  // 2048 * 1536

  bool validateImageDimensions(int width, int height, const std::string& format);
  void warnUnsupportedFeature(const std::string& feature, const std::string& imagePath);
};
