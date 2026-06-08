#include "JpegToFramebufferConverter.h"

#include <esp_heap_caps.h>
#include <esp_task_wdt.h>

#include <BitmapHelpers.h>
#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <JPEGDEC.h>
#include <Logging.h>

#include <Memory.h>

#include <algorithm>
#include <cstring>
#include <cstdlib>
#include <new>

#include "DirectPixelWriter.h"
#include "DitherUtils.h"
#include "PixelCache.h"

namespace {

// Context struct passed through JPEGDEC callbacks to avoid global mutable state.
// The draw callback receives this via pDraw->pUser (set by setUserPointer()).
// The file I/O callbacks receive the FsFile* via pFile->fHandle (set by jpegOpen()).
struct JpegContext {
  GfxRenderer* renderer{nullptr};
  const RenderConfig* config{nullptr};
  int screenWidth{0};
  int screenHeight{0};

  // Source dimensions after JPEGDEC's built-in scaling
  int scaledSrcWidth{0};
  int scaledSrcHeight{0};

  // Final output dimensions
  int dstWidth{0};
  int dstHeight{0};

  // Fine scale in 16.16 fixed-point (ESP32-C3 has no FPU).
  // X and Y axes use separate scale factors: the aspect ratio of the output (dstWidth/dstHeight)
  // may differ from the source (srcWidth/srcHeight) due to integer rounding of displayHeight.
  // Using a single (X-based) scale for both axes causes the wrong srcRow to be skipped
  // during nearest-neighbor downscaling, potentially losing critical image content.
  int32_t fineScaleFPX{1 << 16};  // X: src -> dst column mapping
  int32_t invScaleFPX{1 << 16};   // X: dst -> src column mapping
  int32_t fineScaleFPY{1 << 16};  // Y: src -> dst row mapping
  int32_t invScaleFPY{1 << 16};   // Y: dst -> src row mapping

  PixelCache cache;
  bool caching{false};

  // Tracks dst row coverage so MCU blocks never leave horizontal gaps after downscale.
  int lastDstYWritten{0};

  // Full JPEGDEC grayscale image; scale from this in a second pass (avoids MCU seam artifacts).
  uint8_t* coarseBuffer{nullptr};
  int decodedWidth{0};  // max(blockX + validW) seen during assembly
};

// File I/O callbacks use pFile->fHandle to access the FsFile*,
// avoiding the need for global file state.
void* jpegOpen(const char* filename, int32_t* size) {
  FsFile* f = new FsFile();
  if (!Storage.openFileForRead("JPG", std::string(filename), *f)) {
    delete f;
    return nullptr;
  }
  *size = f->size();
  return f;
}

void jpegClose(void* handle) {
  FsFile* f = reinterpret_cast<FsFile*>(handle);
  if (f) {
    f->close();
    delete f;
  }
}

// JPEGDEC tracks file position via pFile->iPos internally (e.g. JPEGGetMoreData
// checks iPos < iSize to decide whether more data is available). The callbacks
// MUST maintain iPos to match the actual file position, otherwise progressive
// JPEGs with large headers fail during parsing.
int32_t jpegRead(JPEGFILE* pFile, uint8_t* pBuf, int32_t len) {
  FsFile* f = reinterpret_cast<FsFile*>(pFile->fHandle);
  if (!f) return 0;
  int32_t bytesRead = f->read(pBuf, len);
  if (bytesRead < 0) return 0;
  pFile->iPos += bytesRead;
  return bytesRead;
}

int32_t jpegSeek(JPEGFILE* pFile, int32_t pos) {
  FsFile* f = reinterpret_cast<FsFile*>(pFile->fHandle);
  if (!f) return -1;
  if (!f->seek(pos)) return -1;
  pFile->iPos = pos;
  return pos;
}

// JPEGDEC object is ~17 KB due to internal decode buffers.
// Heap-allocate on demand so memory is only used during active decode.
constexpr size_t JPEG_DECODER_APPROX_SIZE = 20 * 1024;
constexpr size_t MIN_FREE_HEAP_FOR_JPEG = JPEG_DECODER_APPROX_SIZE + 16 * 1024;
// Staging buffer budget (PSRAM on S3; full-res ~1080p grayscale fits here).
constexpr size_t MAX_COARSE_BUFFER_BYTES = 2 * 1024 * 1024;

struct CoarseBufferHolder {
  uint8_t* ptr{nullptr};

  ~CoarseBufferHolder() { release(); }

  void release() {
    if (ptr) {
      heap_caps_free(ptr);
      ptr = nullptr;
    }
  }

  bool allocate(size_t bytes) {
    release();
    ptr = static_cast<uint8_t*>(heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!ptr) {
      ptr = static_cast<uint8_t*>(heap_caps_malloc(bytes, MALLOC_CAP_8BIT));
    }
    return ptr != nullptr;
  }
};

int scaledDimension(int srcDim, int jpegScaleDenom) {
  return (srcDim + jpegScaleDenom - 1) / jpegScaleDenom;
}

void setJpegScaleOption(int jpegScaleDenom, int& jpegScaleOption) {
  switch (jpegScaleDenom) {
    case 8:
      jpegScaleOption = JPEG_SCALE_EIGHTH;
      break;
    case 4:
      jpegScaleOption = JPEG_SCALE_QUARTER;
      break;
    case 2:
      jpegScaleOption = JPEG_SCALE_HALF;
      break;
    default:
      jpegScaleOption = 0;
      break;
  }
}

// Pick JPEGDEC scale: prefer full-res (or finest) decode that downscales to dest — never upscale in software.
int selectJpegScaleDenom(int srcWidth, int srcHeight, int destWidth, int destHeight, float targetScale,
                         int& jpegScaleOption) {
  int idealDenom = 1;
  if (targetScale <= 0.125f) {
    idealDenom = 8;
  } else if (targetScale <= 0.25f) {
    idealDenom = 4;
  } else if (targetScale <= 0.5f) {
    idealDenom = 2;
  }

  int finestFit = 8;
  int finestNoUpscale = 0;
  const int denoms[] = {1, 2, 4, 8};
  for (const int denom : denoms) {
    const int sw = scaledDimension(srcWidth, denom);
    const int sh = scaledDimension(srcHeight, denom);
    const size_t bytes = static_cast<size_t>(sw) * static_cast<size_t>(sh);
    if (bytes <= MAX_COARSE_BUFFER_BYTES) {
      finestFit = denom;
      if (sw >= destWidth && sh >= destHeight) {
        finestNoUpscale = denom;
        break;
      }
    }
  }

  int pick = finestNoUpscale > 0 ? finestNoUpscale : finestFit;

  // Thumbnails: avoid decoding more pixels than the contain-fit needs when that tier fits in RAM.
  if (idealDenom > pick) {
    const int sw = scaledDimension(srcWidth, idealDenom);
    const int sh = scaledDimension(srcHeight, idealDenom);
    const size_t bytes = static_cast<size_t>(sw) * static_cast<size_t>(sh);
    if (bytes <= MAX_COARSE_BUFFER_BYTES) {
      pick = idealDenom;
    }
  }

  setJpegScaleOption(pick, jpegScaleOption);
  return pick;
}

// Fill destination rows [fromY, toY) with white (4-level value 3) before the next MCU block.
void fillDstRowGap(JpegContext& ctx, DirectPixelWriter& pw, int fromY, int toY) {
  if (fromY >= toY) return;

  const int cfgX = ctx.config->x;
  const int cfgY = ctx.config->y;
  int dstXStart = 0;
  int dstXEnd = ctx.dstWidth;
  renderDstColumnRange(*ctx.config, cfgX, ctx.dstWidth, ctx.screenWidth, dstXStart, dstXEnd);

  for (int dstY = fromY; dstY < toY; ++dstY) {
    const int outY = cfgY + dstY;
    if (!renderRowYInBounds(*ctx.config, outY, ctx.screenHeight)) {
      continue;
    }
    pw.beginRow(outY);
    for (int dstX = dstXStart; dstX < dstXEnd; ++dstX) {
      const int outX = cfgX + dstX;
      if (renderPixelInBounds(*ctx.config, outX, outY, ctx.screenWidth, ctx.screenHeight)) {
        pw.writePixel(outX, 3);
      }
    }
  }
}

// Choose JPEGDEC's built-in scale factor for coarse downscaling.
// Returns the scale denominator (1, 2, 4, or 8) and sets jpegScaleOption.
int chooseJpegScale(float targetScale, int& jpegScaleOption) {
  if (targetScale <= 0.125f) {
    jpegScaleOption = JPEG_SCALE_EIGHTH;
    return 8;
  }
  if (targetScale <= 0.25f) {
    jpegScaleOption = JPEG_SCALE_QUARTER;
    return 4;
  }
  if (targetScale <= 0.5f) {
    jpegScaleOption = JPEG_SCALE_HALF;
    return 2;
  }
  jpegScaleOption = 0;
  return 1;
}

// Fixed-point 16.16 arithmetic avoids software float emulation on ESP32-C3 (no FPU).
constexpr int FP_SHIFT = 16;
constexpr int32_t FP_ONE = 1 << FP_SHIFT;
constexpr int32_t FP_MASK = FP_ONE - 1;

int jpegDrawCallback(JPEGDRAW* pDraw) {
  JpegContext* ctx = reinterpret_cast<JpegContext*>(pDraw->pUser);
  if (!ctx || !ctx->config || !ctx->renderer) return 0;

  // In EIGHT_BIT_GRAYSCALE mode, pPixels contains 8-bit grayscale values
  // Buffer is densely packed: stride = pDraw->iWidth, valid columns = pDraw->iWidthUsed
  uint8_t* pixels = reinterpret_cast<uint8_t*>(pDraw->pPixels);
  const int stride = pDraw->iWidth;
  const int validW = pDraw->iWidthUsed;
  const int blockH = pDraw->iHeight;

  if (stride <= 0 || blockH <= 0 || validW <= 0) return 1;

  const bool useDithering = ctx->config->useDithering;
  const bool caching = ctx->caching;
  const int32_t fineScaleFPX = ctx->fineScaleFPX;
  const int32_t invScaleFPX = ctx->invScaleFPX;
  const int32_t fineScaleFPY = ctx->fineScaleFPY;
  const int32_t invScaleFPY = ctx->invScaleFPY;
  GfxRenderer& renderer = *ctx->renderer;
  const int cfgX = ctx->config->x;
  const int cfgY = ctx->config->y;
  const int blockX = pDraw->x;
  const int blockY = pDraw->y;
  if ((blockY & 15) == 0) {
    esp_task_wdt_reset();
  }

  if (ctx->coarseBuffer) {
    for (int y = 0; y < blockH; ++y) {
      const int sy = blockY + y;
      if (sy < 0 || sy >= ctx->scaledSrcHeight) {
        continue;
      }
      int dx = blockX;
      int copyW = validW;
      if (dx < 0) {
        copyW += dx;
        dx = 0;
      }
      if (dx >= ctx->scaledSrcWidth || copyW <= 0) {
        continue;
      }
      if (dx + copyW > ctx->scaledSrcWidth) {
        copyW = ctx->scaledSrcWidth - dx;
      }
      memcpy(ctx->coarseBuffer + static_cast<size_t>(sy) * ctx->scaledSrcWidth + dx, &pixels[y * stride], copyW);
    }
    const int coverage = blockX + validW;
    if (coverage > ctx->decodedWidth) {
      ctx->decodedWidth = coverage;
    }
    return 1;
  }

  // Determine destination pixel range covered by this source block
  const int srcYEnd = blockY + blockH;
  const int srcXEnd = blockX + validW;

  int dstYStart = (int)((int64_t)blockY * fineScaleFPY >> FP_SHIFT);
  int dstYEnd = (srcYEnd >= ctx->scaledSrcHeight) ? ctx->dstHeight : (int)((int64_t)srcYEnd * fineScaleFPY >> FP_SHIFT);
  int dstXStart = (int)((int64_t)blockX * fineScaleFPX >> FP_SHIFT);
  int dstXEnd = (srcXEnd >= ctx->scaledSrcWidth) ? ctx->dstWidth : (int)((int64_t)srcXEnd * fineScaleFPX >> FP_SHIFT);

  // Pre-clamp destination ranges to screen bounds (eliminates per-pixel screen checks)
  int clampYMax = ctx->dstHeight;
  if (ctx->screenHeight - cfgY < clampYMax) clampYMax = ctx->screenHeight - cfgY;
  if (dstYStart < -cfgY) dstYStart = -cfgY;
  if (dstYEnd > clampYMax) dstYEnd = clampYMax;

  int clampXMax = ctx->dstWidth;
  if (ctx->screenWidth - cfgX < clampXMax) clampXMax = ctx->screenWidth - cfgX;
  if (dstXStart < -cfgX) dstXStart = -cfgX;
  if (dstXEnd > clampXMax) dstXEnd = clampXMax;

  renderDstColumnRange(*ctx->config, cfgX, ctx->dstWidth, ctx->screenWidth, dstXStart, dstXEnd);
  if (ctx->config->clipW > 0 && ctx->config->clipH > 0) {
    const int clipT = ctx->config->clipY - cfgY;
    const int clipB = ctx->config->clipY + ctx->config->clipH - cfgY;
    if (dstYStart < clipT) dstYStart = clipT;
    if (dstYEnd > clipB) dstYEnd = clipB;
  }

  if (dstYStart >= dstYEnd || dstXStart >= dstXEnd) return 1;

  DirectPixelWriter pw;
  pw.init(renderer);

  // Never re-draw rows already written by a prior MCU block (causes horizontal seams).
  if (dstYStart > ctx->lastDstYWritten) {
    fillDstRowGap(*ctx, pw, ctx->lastDstYWritten, dstYStart);
    ctx->lastDstYWritten = dstYStart;
  }
  if (dstYStart < ctx->lastDstYWritten) {
    dstYStart = ctx->lastDstYWritten;
  }
  if (dstYStart >= dstYEnd) return 1;

  // Pre-compute orientation and render-mode state once per callback invocation

  DirectCacheWriter cw;
  if (caching) {
    cw.init(ctx->cache.buffer, ctx->cache.bytesPerRow, ctx->cache.originX);
  }

  // === 1:1 fast path: no scaling math ===
  if (fineScaleFPX == FP_ONE && fineScaleFPY == FP_ONE) {
    for (int dstY = dstYStart; dstY < dstYEnd; dstY++) {
      const int outY = cfgY + dstY;
      pw.beginRow(outY);
      if (caching) cw.beginRow(outY, ctx->config->y);
      const uint8_t* row = &pixels[(dstY - blockY) * stride];
      for (int dstX = dstXStart; dstX < dstXEnd; dstX++) {
        const int outX = cfgX + dstX;
        uint8_t gray = row[dstX - blockX];
        uint8_t dithered;
        if (useDithering) {
          dithered = applyBayerDither4Level(gray, outX, outY);
        } else {
          dithered = gray / 85;
          if (dithered > 3) dithered = 3;
        }
        pw.writePixel(outX, dithered);
        if (caching) cw.writePixel(outX, dithered);
      }
    }
    if (dstYEnd > ctx->lastDstYWritten) {
      ctx->lastDstYWritten = dstYEnd;
    }
    return 1;
  }

  // === Bilinear interpolation (upscale only; downscale uses nearest below) ===
  if (fineScaleFPX > FP_ONE && fineScaleFPY > FP_ONE) {
    // Pre-compute safe X range where lx0 and lx0+1 are both in [0, validW-1].
    // Only the left/right edge pixels (typically 0-2 and 1-8 respectively) need clamping.
    int safeXStart = (int)(((int64_t)blockX * fineScaleFPX + FP_MASK) >> FP_SHIFT);
    int safeXEnd = (int)((int64_t)(blockX + validW - 1) * fineScaleFPX >> FP_SHIFT);
    if (safeXStart < dstXStart) safeXStart = dstXStart;
    if (safeXEnd > dstXEnd) safeXEnd = dstXEnd;
    if (safeXStart > safeXEnd) safeXEnd = safeXStart;

    for (int dstY = dstYStart; dstY < dstYEnd; dstY++) {
      const int outY = cfgY + dstY;
      pw.beginRow(outY);
      if (caching) cw.beginRow(outY, ctx->config->y);
      const int32_t srcFyFP = dstY * invScaleFPY;
      const int32_t fy = srcFyFP & FP_MASK;
      const int32_t fyInv = FP_ONE - fy;
      int ly0 = (srcFyFP >> FP_SHIFT) - blockY;
      int ly1 = ly0 + 1;
      if (ly0 < 0) ly0 = 0;
      if (ly0 >= blockH) ly0 = blockH - 1;
      if (ly1 >= blockH) ly1 = blockH - 1;

      const uint8_t* row0 = &pixels[ly0 * stride];
      const uint8_t* row1 = &pixels[ly1 * stride];

      // Left edge (with X boundary clamping)
      for (int dstX = dstXStart; dstX < safeXStart; dstX++) {
        const int outX = cfgX + dstX;
        const int32_t srcFxFP = dstX * invScaleFPX;
        const int32_t fx = srcFxFP & FP_MASK;
        const int32_t fxInv = FP_ONE - fx;
        int lx0 = (srcFxFP >> FP_SHIFT) - blockX;
        int lx1 = lx0 + 1;
        if (lx0 < 0) lx0 = 0;
        if (lx1 < 0) lx1 = 0;
        if (lx0 >= validW) lx0 = validW - 1;
        if (lx1 >= validW) lx1 = validW - 1;

        int top = ((int)row0[lx0] * fxInv + (int)row0[lx1] * fx) >> FP_SHIFT;
        int bot = ((int)row1[lx0] * fxInv + (int)row1[lx1] * fx) >> FP_SHIFT;
        uint8_t gray = (uint8_t)((top * fyInv + bot * fy) >> FP_SHIFT);

        uint8_t dithered;
        if (useDithering) {
          dithered = applyBayerDither4Level(gray, outX, outY);
        } else {
          dithered = gray / 85;
          if (dithered > 3) dithered = 3;
        }
        pw.writePixel(outX, dithered);
        if (caching) cw.writePixel(outX, dithered);
      }

      // Interior (no X boundary checks — lx0 and lx0+1 guaranteed in bounds)
      for (int dstX = safeXStart; dstX < safeXEnd; dstX++) {
        const int outX = cfgX + dstX;
        const int32_t srcFxFP = dstX * invScaleFPX;
        const int32_t fx = srcFxFP & FP_MASK;
        const int32_t fxInv = FP_ONE - fx;
        const int lx0 = (srcFxFP >> FP_SHIFT) - blockX;

        int top = ((int)row0[lx0] * fxInv + (int)row0[lx0 + 1] * fx) >> FP_SHIFT;
        int bot = ((int)row1[lx0] * fxInv + (int)row1[lx0 + 1] * fx) >> FP_SHIFT;
        uint8_t gray = (uint8_t)((top * fyInv + bot * fy) >> FP_SHIFT);

        uint8_t dithered;
        if (useDithering) {
          dithered = applyBayerDither4Level(gray, outX, outY);
        } else {
          dithered = gray / 85;
          if (dithered > 3) dithered = 3;
        }
        pw.writePixel(outX, dithered);
        if (caching) cw.writePixel(outX, dithered);
      }

      // Right edge (with X boundary clamping)
      for (int dstX = safeXEnd; dstX < dstXEnd; dstX++) {
        const int outX = cfgX + dstX;
        const int32_t srcFxFP = dstX * invScaleFPX;
        const int32_t fx = srcFxFP & FP_MASK;
        const int32_t fxInv = FP_ONE - fx;
        int lx0 = (srcFxFP >> FP_SHIFT) - blockX;
        int lx1 = lx0 + 1;
        if (lx0 >= validW) lx0 = validW - 1;
        if (lx1 >= validW) lx1 = validW - 1;

        int top = ((int)row0[lx0] * fxInv + (int)row0[lx1] * fx) >> FP_SHIFT;
        int bot = ((int)row1[lx0] * fxInv + (int)row1[lx1] * fx) >> FP_SHIFT;
        uint8_t gray = (uint8_t)((top * fyInv + bot * fy) >> FP_SHIFT);

        uint8_t dithered;
        if (useDithering) {
          dithered = applyBayerDither4Level(gray, outX, outY);
        } else {
          dithered = gray / 85;
          if (dithered > 3) dithered = 3;
        }
        pw.writePixel(outX, dithered);
        if (caching) cw.writePixel(outX, dithered);
      }
    }
    if (dstYEnd > ctx->lastDstYWritten) {
      ctx->lastDstYWritten = dstYEnd;
    }
    return 1;
  }

  // === Nearest-neighbor downscale (MCU direct path fallback) ===
  for (int dstY = dstYStart; dstY < dstYEnd; dstY++) {
    const int outY = cfgY + dstY;
    pw.beginRow(outY);
    if (caching) cw.beginRow(outY, ctx->config->y);
    const int32_t srcFyFP = dstY * invScaleFPY;
    int ly = (srcFyFP >> FP_SHIFT) - blockY;
    if (ly < 0) ly = 0;
    if (ly >= blockH) ly = blockH - 1;
    const uint8_t* row = &pixels[ly * stride];

    for (int dstX = dstXStart; dstX < dstXEnd; dstX++) {
      const int outX = cfgX + dstX;
      const int32_t srcFxFP = dstX * invScaleFPX;
      int lx = (srcFxFP >> FP_SHIFT) - blockX;
      if (lx < 0) lx = 0;
      if (lx >= validW) lx = validW - 1;
      uint8_t gray = row[lx];

      uint8_t dithered;
      if (useDithering) {
        dithered = applyBayerDither4Level(gray, outX, outY);
      } else {
        dithered = gray / 85;
        if (dithered > 3) dithered = 3;
      }
      pw.writePixel(outX, dithered);
      if (caching) cw.writePixel(outX, dithered);
    }
  }

  if (dstYEnd > ctx->lastDstYWritten) {
    ctx->lastDstYWritten = dstYEnd;
  }

  return 1;
}

// Area-average a destination pixel from the coarse grayscale buffer (same approach as JpegToBmpConverter).
uint8_t sampleCoarseArea(const uint8_t* coarse, int stride, int srcWidth, int srcHeight, int dstWidth, int dstHeight,
                         int dstX, int dstY) {
  const int x0 = static_cast<int>((static_cast<int64_t>(dstX) * srcWidth) / dstWidth);
  const int x1 = static_cast<int>((static_cast<int64_t>(dstX + 1) * srcWidth) / dstWidth);
  const int y0 = static_cast<int>((static_cast<int64_t>(dstY) * srcHeight) / dstHeight);
  const int y1 = static_cast<int>((static_cast<int64_t>(dstY + 1) * srcHeight) / dstHeight);

  int xEnd = x1;
  if (xEnd <= x0) {
    xEnd = x0 + 1;
  }
  if (xEnd > srcWidth) {
    xEnd = srcWidth;
  }

  int yEnd = y1;
  if (yEnd <= y0) {
    yEnd = y0 + 1;
  }
  if (yEnd > srcHeight) {
    yEnd = srcHeight;
  }

  int sum = 0;
  int count = 0;
  for (int sy = y0; sy < yEnd; ++sy) {
    const uint8_t* row = coarse + static_cast<size_t>(sy) * stride;
    for (int sx = x0; sx < xEnd; ++sx) {
      sum += row[sx];
      ++count;
    }
  }

  if (count == 0) {
    const int sx = (x0 < srcWidth) ? x0 : (srcWidth - 1);
    const int sy = (y0 < srcHeight) ? y0 : (srcHeight - 1);
    return coarse[static_cast<size_t>(sy) * stride + sx];
  }
  return static_cast<uint8_t>(sum / count);
}

void renderCoarseToFramebuffer(JpegContext& ctx) {
  if (!ctx.coarseBuffer || !ctx.renderer || !ctx.config) {
    return;
  }

  const bool useDithering = ctx.config->useDithering;
  const bool caching = ctx.caching;
  const int cfgX = ctx.config->x;
  const int cfgY = ctx.config->y;
  const int srcWidth = (ctx.decodedWidth > 0) ? ctx.decodedWidth : ctx.scaledSrcWidth;
  const int srcHeight = ctx.scaledSrcHeight;
  const int stride = ctx.scaledSrcWidth;
  const int dstWidth = ctx.dstWidth;
  const int dstHeight = ctx.dstHeight;

  auto grayRow = makeUniqueNoThrow<uint8_t[]>(dstWidth);
  if (!grayRow) {
    LOG_ERR("JPG", "OOM: grayscale row for coarse render");
    return;
  }

  std::unique_ptr<AtkinsonDitherer> atkinson;
  if (useDithering) {
    atkinson = makeUniqueNoThrow<AtkinsonDitherer>(dstWidth);
    if (!atkinson) {
      LOG_ERR("JPG", "OOM: Atkinson ditherer");
      return;
    }
  }

  DirectPixelWriter pw;
  pw.init(*ctx.renderer);

  DirectCacheWriter cw;
  if (caching) {
    cw.init(ctx.cache.buffer, ctx.cache.bytesPerRow, ctx.cache.originX);
  }

  int dstXStart = 0;
  int dstXEnd = dstWidth;
  renderDstColumnRange(*ctx.config, cfgX, dstWidth, ctx.screenWidth, dstXStart, dstXEnd);
  if (dstXStart >= dstXEnd) {
    ctx.lastDstYWritten = dstHeight;
    return;
  }

  for (int dstY = 0; dstY < dstHeight; ++dstY) {
    if ((dstY & 15) == 0) {
      esp_task_wdt_reset();
    }

    if (ctx.config->clipW > 0 && ctx.config->clipH > 0) {
      const int clipT = ctx.config->clipY - cfgY;
      const int clipB = ctx.config->clipY + ctx.config->clipH - cfgY;
      if (dstY < clipT || dstY >= clipB) {
        continue;
      }
    }

    const int outY = cfgY + dstY;
    if (!renderRowYInBounds(*ctx.config, outY, ctx.screenHeight)) {
      continue;
    }

    for (int dstX = 0; dstX < dstWidth; ++dstX) {
      grayRow[dstX] = sampleCoarseArea(ctx.coarseBuffer, stride, srcWidth, srcHeight, dstWidth, dstHeight, dstX, dstY);
    }

    pw.beginRow(outY);
    if (caching) {
      cw.beginRow(outY, ctx.config->y);
    }

    for (int dstX = dstXStart; dstX < dstXEnd; ++dstX) {
      const int outX = cfgX + dstX;
      if (!renderPixelInBounds(*ctx.config, outX, outY, ctx.screenWidth, ctx.screenHeight)) {
        continue;
      }

      uint8_t dithered;
      if (useDithering) {
        dithered = atkinson->processPixel(grayRow[dstX], dstX);
      } else {
        dithered = grayRow[dstX] / 85;
        if (dithered > 3) dithered = 3;
      }
      pw.writePixel(outX, dithered);
      if (caching) {
        cw.writePixel(outX, dithered);
      }
    }

    if (atkinson) {
      atkinson->nextRow();
    }
  }

  ctx.lastDstYWritten = dstHeight;
}

void fillUncoveredJpegRows(JpegContext& ctx) {
  if (!ctx.renderer || !ctx.config || ctx.lastDstYWritten >= ctx.dstHeight) {
    return;
  }

  DirectPixelWriter pw;
  pw.init(*ctx.renderer);
  const int cfgX = ctx.config->x;
  const int cfgY = ctx.config->y;
  int dstXStart = 0;
  int dstXEnd = ctx.dstWidth;
  renderDstColumnRange(*ctx.config, cfgX, ctx.dstWidth, ctx.screenWidth, dstXStart, dstXEnd);

  for (int dstY = ctx.lastDstYWritten; dstY < ctx.dstHeight; ++dstY) {
    const int outY = cfgY + dstY;
    if (!renderRowYInBounds(*ctx.config, outY, ctx.screenHeight)) {
      continue;
    }
    pw.beginRow(outY);
    for (int dstX = dstXStart; dstX < dstXEnd; ++dstX) {
      const int outX = cfgX + dstX;
      if (renderPixelInBounds(*ctx.config, outX, outY, ctx.screenWidth, ctx.screenHeight)) {
        pw.writePixel(outX, 3);
      }
    }
  }
}

}  // namespace

bool JpegToFramebufferConverter::getDimensionsStatic(const std::string& imagePath, ImageDimensions& out) {
  size_t freeHeap = ESP.getFreeHeap();
  if (freeHeap < MIN_FREE_HEAP_FOR_JPEG) {
    LOG_ERR("JPG", "Not enough heap for JPEG decoder (%u free, need %u)", freeHeap, MIN_FREE_HEAP_FOR_JPEG);
    return false;
  }

  JPEGDEC* jpeg = new (std::nothrow) JPEGDEC();
  if (!jpeg) {
    LOG_ERR("JPG", "Failed to allocate JPEG decoder for dimensions");
    return false;
  }

  int rc = jpeg->open(imagePath.c_str(), jpegOpen, jpegClose, jpegRead, jpegSeek, nullptr);
  if (rc != 1) {
    LOG_ERR("JPG", "Failed to open JPEG for dimensions (err=%d): %s", jpeg->getLastError(), imagePath.c_str());
    delete jpeg;
    return false;
  }

  out.width = jpeg->getWidth();
  out.height = jpeg->getHeight();
  LOG_DBG("JPG", "Image dimensions: %dx%d", out.width, out.height);

  jpeg->close();
  delete jpeg;
  return true;
}

bool JpegToFramebufferConverter::decodeToFramebuffer(const std::string& imagePath, GfxRenderer& renderer,
                                                     const RenderConfig& config) {
  LOG_DBG("JPG", "Decoding JPEG: %s", imagePath.c_str());

  size_t freeHeap = ESP.getFreeHeap();
  if (freeHeap < MIN_FREE_HEAP_FOR_JPEG) {
    LOG_ERR("JPG", "Not enough heap for JPEG decoder (%u free, need %u)", freeHeap, MIN_FREE_HEAP_FOR_JPEG);
    return false;
  }

  JPEGDEC* jpeg = new (std::nothrow) JPEGDEC();
  if (!jpeg) {
    LOG_ERR("JPG", "Failed to allocate JPEG decoder");
    return false;
  }

  JpegContext ctx;
  ctx.renderer = &renderer;
  ctx.config = &config;
  ctx.screenWidth = renderer.getScreenWidth();
  ctx.screenHeight = renderer.getScreenHeight();

  int rc = jpeg->open(imagePath.c_str(), jpegOpen, jpegClose, jpegRead, jpegSeek, jpegDrawCallback);
  if (rc != 1) {
    LOG_ERR("JPG", "Failed to open JPEG (err=%d): %s", jpeg->getLastError(), imagePath.c_str());
    delete jpeg;
    return false;
  }

  int srcWidth = jpeg->getWidth();
  int srcHeight = jpeg->getHeight();

  if (srcWidth <= 0 || srcHeight <= 0) {
    LOG_ERR("JPG", "Invalid JPEG dimensions: %dx%d", srcWidth, srcHeight);
    jpeg->close();
    delete jpeg;
    return false;
  }

  if (!validateImageDimensions(srcWidth, srcHeight, "JPEG")) {
    jpeg->close();
    delete jpeg;
    return false;
  }

  bool isProgressive = jpeg->getJPEGType() == JPEG_MODE_PROGRESSIVE;
  if (isProgressive) {
    LOG_INF("JPG", "Progressive JPEG detected - decoding DC coefficients only (lower quality)");
  }

  // Calculate overall target scale
  float targetScale;
  int destWidth, destHeight;

  if (config.useExactDimensions && config.maxWidth > 0 && config.maxHeight > 0) {
    destWidth = config.maxWidth;
    destHeight = config.maxHeight;
    targetScale = std::min(static_cast<float>(destWidth) / srcWidth, static_cast<float>(destHeight) / srcHeight);
    if (targetScale > 1.0f) {
      targetScale = 1.0f;
    }
  } else {
    float scaleX = (config.maxWidth > 0 && srcWidth > config.maxWidth) ? (float)config.maxWidth / srcWidth : 1.0f;
    float scaleY = (config.maxHeight > 0 && srcHeight > config.maxHeight) ? (float)config.maxHeight / srcHeight : 1.0f;
    targetScale = (scaleX < scaleY) ? scaleX : scaleY;
    if (targetScale > 1.0f) targetScale = 1.0f;

    destWidth = (int)(srcWidth * targetScale);
    destHeight = (int)(srcHeight * targetScale);
  }

  // Choose JPEGDEC built-in scaling for coarse downscaling.
  // Progressive JPEGs: JPEGDEC forces JPEG_SCALE_EIGHTH internally (DC-only
  // decode produces 1/8 resolution). We must match this to avoid the if/else
  // priority chain in DecodeJPEG selecting a different scale.
  int jpegScaleOption;
  int jpegScaleDenom;
  if (isProgressive) {
    jpegScaleOption = JPEG_SCALE_EIGHTH;
    jpegScaleDenom = 8;
  } else {
    jpegScaleDenom = selectJpegScaleDenom(srcWidth, srcHeight, destWidth, destHeight, targetScale, jpegScaleOption);
  }

  if (destWidth <= 0 || destHeight <= 0) {
    LOG_ERR("JPG", "Degenerate output dimensions %dx%d for %s, skipping render", destWidth, destHeight,
            imagePath.c_str());
    jpeg->close();
    delete jpeg;
    return false;
  }

  ctx.scaledSrcWidth = scaledDimension(srcWidth, jpegScaleDenom);
  ctx.scaledSrcHeight = scaledDimension(srcHeight, jpegScaleDenom);
  ctx.decodedWidth = 0;
  ctx.dstWidth = destWidth;
  ctx.dstHeight = destHeight;
  ctx.fineScaleFPX = (int32_t)((int64_t)destWidth * FP_ONE / ctx.scaledSrcWidth);
  ctx.invScaleFPX = (int32_t)((int64_t)ctx.scaledSrcWidth * FP_ONE / destWidth);
  ctx.fineScaleFPY = (int32_t)((int64_t)destHeight * FP_ONE / ctx.scaledSrcHeight);
  ctx.invScaleFPY = (int32_t)((int64_t)ctx.scaledSrcHeight * FP_ONE / destHeight);

  LOG_DBG("JPG", "JPEG %dx%d -> %dx%d (scale %.2f, jpegScale 1/%d, fineScale %.2f)%s", srcWidth, srcHeight, destWidth,
          destHeight, targetScale, jpegScaleDenom, (float)destWidth / ctx.scaledSrcWidth,
          isProgressive ? " [progressive]" : "");

  // Set pixel type to 8-bit grayscale (must be after open())
  jpeg->setPixelType(EIGHT_BIT_GRAYSCALE);
  jpeg->setUserPointer(&ctx);

  // Allocate cache buffer using final output dimensions
  ctx.caching = !config.cachePath.empty();
  if (ctx.caching) {
    if (!ctx.cache.allocate(destWidth, destHeight, config.x, config.y)) {
      LOG_ERR("JPG", "Failed to allocate cache buffer, continuing without caching");
      ctx.caching = false;
    }
  }

  ctx.lastDstYWritten = 0;

  CoarseBufferHolder coarseBuf;
  int allocDenom = jpegScaleDenom;
  while (allocDenom <= 8 && !ctx.coarseBuffer) {
    ctx.scaledSrcWidth = scaledDimension(srcWidth, allocDenom);
    ctx.scaledSrcHeight = scaledDimension(srcHeight, allocDenom);
    const size_t coarseBytes =
        static_cast<size_t>(ctx.scaledSrcWidth) * static_cast<size_t>(ctx.scaledSrcHeight);
    if (coarseBytes > 0 && freeHeap >= MIN_FREE_HEAP_FOR_JPEG + 32 * 1024) {
      if (coarseBuf.allocate(coarseBytes)) {
        memset(coarseBuf.ptr, 255, coarseBytes);
        ctx.coarseBuffer = coarseBuf.ptr;
        jpegScaleDenom = allocDenom;
        setJpegScaleOption(allocDenom, jpegScaleOption);
        ctx.fineScaleFPX = (int32_t)((int64_t)destWidth * FP_ONE / ctx.scaledSrcWidth);
        ctx.invScaleFPX = (int32_t)((int64_t)ctx.scaledSrcWidth * FP_ONE / destWidth);
        ctx.fineScaleFPY = (int32_t)((int64_t)destHeight * FP_ONE / ctx.scaledSrcHeight);
        ctx.invScaleFPY = (int32_t)((int64_t)ctx.scaledSrcHeight * FP_ONE / destHeight);
        LOG_DBG("JPG", "Coarse buffer %u bytes (%dx%d), jpegScale 1/%d", coarseBytes, ctx.scaledSrcWidth,
                ctx.scaledSrcHeight, allocDenom);
      }
    }
    if (!ctx.coarseBuffer) {
      allocDenom *= 2;
    }
  }
  if (!ctx.coarseBuffer) {
    LOG_DBG("JPG", "Coarse buffer unavailable, using direct MCU render");
  }

  unsigned long decodeStart = millis();
  rc = jpeg->decode(0, 0, jpegScaleOption);
  unsigned long decodeTime = millis() - decodeStart;

  if (rc != 1) {
    LOG_ERR("JPG", "Decode failed (rc=%d, lastError=%d)", rc, jpeg->getLastError());
    jpeg->close();
    delete jpeg;
    return false;
  }

  jpeg->close();
  delete jpeg;
  LOG_DBG("JPG", "JPEG decoding complete - render time: %lu ms", decodeTime);

  if (ctx.coarseBuffer) {
    renderCoarseToFramebuffer(ctx);
  } else {
    fillUncoveredJpegRows(ctx);
  }

  // Write cache file if caching was enabled
  if (ctx.caching) {
    ctx.cache.writeToFile(config.cachePath);
  }

  return true;
}

bool JpegToFramebufferConverter::supportsFormat(const std::string& extension) {
  return FsHelpers::hasJpgExtension(extension);
}
