#pragma once

#include <blend2d/blend2d.h>

#include <string>
#include <vector>

struct SvgRenderOptions {
  std::string inputPath;
  std::string outputPath;
  int width = 0;
  int height = 0;
  std::vector<std::string> fontPaths;
};

bool renderSvgToImage(const SvgRenderOptions& options, BLImage& image);
bool renderSvgToPng(const SvgRenderOptions& options);
