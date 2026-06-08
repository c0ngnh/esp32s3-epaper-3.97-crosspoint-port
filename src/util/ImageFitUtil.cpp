#include "ImageFitUtil.h"

#include <cmath>

void computeContainFit(const int imgW, const int imgH, const int pageW, const int pageH, int& drawW, int& drawH,
                       int& x, int& y) {
  drawW = imgW;
  drawH = imgH;
  x = 0;
  y = 0;
  if (drawW > pageW || drawH > pageH) {
    const float ratio = static_cast<float>(drawW) / static_cast<float>(drawH);
    const float screenRatio = static_cast<float>(pageW) / static_cast<float>(pageH);
    if (ratio > screenRatio) {
      drawW = pageW;
      drawH = static_cast<int>(std::round(static_cast<float>(pageW) / ratio));
    } else {
      drawH = pageH;
      drawW = static_cast<int>(std::round(static_cast<float>(pageH) * ratio));
    }
    x = (pageW - drawW) / 2;
    y = (pageH - drawH) / 2;
  } else {
    x = (pageW - drawW) / 2;
    y = (pageH - drawH) / 2;
  }
}

ImageFit pickBestImageFit(GfxRenderer& renderer, const int imgW, const int imgH) {
  const auto savedOrientation = renderer.getOrientation();
  ImageFit best{};

  const GfxRenderer::Orientation orientations[] = {GfxRenderer::Portrait,
                                                   GfxRenderer::LandscapeCounterClockwise};
  int64_t bestArea = 0;
  for (const auto orientation : orientations) {
    renderer.setOrientation(orientation);
    const int pageW = renderer.getScreenWidth();
    const int pageH = renderer.getScreenHeight();
    int drawW = 0;
    int drawH = 0;
    int x = 0;
    int y = 0;
    computeContainFit(imgW, imgH, pageW, pageH, drawW, drawH, x, y);
    const int64_t area = static_cast<int64_t>(drawW) * drawH;
    if (area > bestArea) {
      bestArea = area;
      best.orientation = orientation;
      best.drawW = drawW;
      best.drawH = drawH;
      best.x = x;
      best.y = y;
    }
  }

  renderer.setOrientation(savedOrientation);
  renderer.setOrientation(best.orientation);
  return best;
}
