#pragma once

#include "Button.h"

#include <blend2d/blend2d.h>

#include <filesystem>
#include <string>
#include <vector>

namespace Blend2DUI {

class FontManager {
 public:
  static BLFont loadFont(UI_ButtonResources& resources, const UI_ButtonStyle& style);
  static std::filesystem::path resolveFontPath(const std::string& assetBasePath, const UI_ButtonStyle& style);
  static std::vector<std::filesystem::path> fontCandidates(const std::string& assetBasePath,
                                                           const std::string& fontName);

 private:
  static bool hasFontExtension(const std::string& name);
  static std::filesystem::path resolveAssetPath(const std::string& assetBasePath, const std::string& path);
  static std::filesystem::path windowsFontsDirectory();
  static void appendWindowsFontCandidate(std::vector<std::filesystem::path>& candidates,
                                         const std::filesystem::path& fontsDir,
                                         const char* fileName);
  static void appendAppleFontCandidate(std::vector<std::filesystem::path>& candidates, const char* fileName);
};

}  // namespace Blend2DUI
