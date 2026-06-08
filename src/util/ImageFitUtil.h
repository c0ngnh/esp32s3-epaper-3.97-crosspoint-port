#pragma once

#include <GfxRenderer.h>

struct ImageFit {
  GfxRenderer::Orientation orientation = GfxRenderer::Portrait;
  int x = 0;
  int y = 0;
  int drawW = 0;
  int drawH = 0;
};

void computeContainFit(int imgW, int imgH, int pageW, int pageH, int& drawW, int& drawH, int& x, int& y);

// Tries portrait and landscape; picks orientation that maximizes contained image area.
// Leaves renderer set to the chosen orientation.
ImageFit pickBestImageFit(GfxRenderer& renderer, int imgW, int imgH);
