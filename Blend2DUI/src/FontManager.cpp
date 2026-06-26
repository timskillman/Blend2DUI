#include "FontManager.h"
#include "Utility.h"

#include <cstdlib>

namespace Blend2DUI {

bool FontManager::hasFontExtension(const std::string& name) {
  const std::string ext = lower(std::filesystem::path(name).extension().string());
  return ext == ".ttf" || ext == ".ttc" || ext == ".otf";
}

std::filesystem::path FontManager::resolveAssetPath(const std::string& assetBasePath, const std::string& path) {
  return Blend2DUI::resolveAssetPath(assetBasePath, path);
}

std::filesystem::path FontManager::windowsFontsDirectory() {
#ifdef _WIN32
  const char* windowsDir = std::getenv("WINDIR");
  if (!windowsDir || !*windowsDir) {
    windowsDir = std::getenv("SystemRoot");
  }

  if (windowsDir && *windowsDir) {
    return std::filesystem::path(windowsDir) / "Fonts";
  }
#endif

  return {};
}

void FontManager::appendWindowsFontCandidate(std::vector<std::filesystem::path>& candidates,
                                             const std::filesystem::path& fontsDir,
                                             const char* fileName) {
  if (!fontsDir.empty()) {
    candidates.emplace_back(fontsDir / fileName);
  }
}

void FontManager::appendAppleFontCandidate(std::vector<std::filesystem::path>& candidates, const char* fileName) {
#ifdef __APPLE__
  candidates.emplace_back(std::filesystem::path("/System/Library/Fonts") / fileName);
  candidates.emplace_back(std::filesystem::path("/System/Library/Fonts/Supplemental") / fileName);
  candidates.emplace_back(std::filesystem::path("/Library/Fonts") / fileName);
#else
  (void)candidates;
  (void)fileName;
#endif
}

std::vector<std::filesystem::path> FontManager::fontCandidates(const std::string& assetBasePath,
                                                               const std::string& fontName) {
  namespace fs = std::filesystem;
  std::string name = fontName.empty() ? "DejaVuSans" : fontName;
  if (!hasFontExtension(name)) name += ".ttf";

  const std::string key = lower(fontName);
  std::vector<fs::path> candidates;
  candidates.push_back(resolveAssetPath(assetBasePath, name));

#ifdef _WIN32
  const fs::path windowsFontsDir = windowsFontsDirectory();
  if (key == "arial") {
    appendWindowsFontCandidate(candidates, windowsFontsDir, "arial.ttf");
  }
  if (key.find("dejavu") != std::string::npos || key == "sans" || key == "dejavusans") {
    appendWindowsFontCandidate(candidates, windowsFontsDir, "YuGothR.ttc");
    appendWindowsFontCandidate(candidates, windowsFontsDir, "YuGothM.ttc");
    appendWindowsFontCandidate(candidates, windowsFontsDir, "meiryo.ttc");
    appendWindowsFontCandidate(candidates, windowsFontsDir, "segoeui.ttf");
    appendWindowsFontCandidate(candidates, windowsFontsDir, "arial.ttf");
  }
  if (key.find("serif") != std::string::npos) {
    appendWindowsFontCandidate(candidates, windowsFontsDir, "times.ttf");
    appendWindowsFontCandidate(candidates, windowsFontsDir, "georgia.ttf");
  }
  if (key.find("mono") != std::string::npos) {
    appendWindowsFontCandidate(candidates, windowsFontsDir, "consola.ttf");
    appendWindowsFontCandidate(candidates, windowsFontsDir, "cour.ttf");
  }
  if (key.find("liberation") != std::string::npos) {
    appendWindowsFontCandidate(candidates, windowsFontsDir, "arial.ttf");
    appendWindowsFontCandidate(candidates, windowsFontsDir, "calibri.ttf");
  }

  appendWindowsFontCandidate(candidates, windowsFontsDir, "YuGothR.ttc");
  appendWindowsFontCandidate(candidates, windowsFontsDir, "YuGothM.ttc");
  appendWindowsFontCandidate(candidates, windowsFontsDir, "meiryo.ttc");
  appendWindowsFontCandidate(candidates, windowsFontsDir, "segoeui.ttf");
  appendWindowsFontCandidate(candidates, windowsFontsDir, "arial.ttf");
  appendWindowsFontCandidate(candidates, windowsFontsDir, "calibri.ttf");
  appendWindowsFontCandidate(candidates, windowsFontsDir, "msgothic.ttc");
#endif

#ifdef __APPLE__
  if (key == "arial") {
    appendAppleFontCandidate(candidates, "Arial.ttf");
  }
  if (key.find("dejavu") != std::string::npos || key == "sans" || key == "dejavusans" || key.empty()) {
    appendAppleFontCandidate(candidates, "Hiragino Sans GB.ttc");
    appendAppleFontCandidate(candidates, "ヒラギノ角ゴシック W3.ttc");
    appendAppleFontCandidate(candidates, "ヒラギノ角ゴシック W6.ttc");
    appendAppleFontCandidate(candidates, "SFNS.ttf");
    appendAppleFontCandidate(candidates, "Helvetica.ttc");
    appendAppleFontCandidate(candidates, "HelveticaNeue.ttc");
    appendAppleFontCandidate(candidates, "Arial.ttf");
  }
  if (key.find("serif") != std::string::npos) {
    appendAppleFontCandidate(candidates, "NewYork.ttf");
    appendAppleFontCandidate(candidates, "Times New Roman.ttf");
  }
  if (key.find("mono") != std::string::npos) {
    appendAppleFontCandidate(candidates, "SFNSMono.ttf");
    appendAppleFontCandidate(candidates, "Menlo.ttc");
    appendAppleFontCandidate(candidates, "Monaco.ttf");
  }
  if (key.find("liberation") != std::string::npos) {
    appendAppleFontCandidate(candidates, "SFNS.ttf");
    appendAppleFontCandidate(candidates, "Helvetica.ttc");
    appendAppleFontCandidate(candidates, "Arial.ttf");
  }

  appendAppleFontCandidate(candidates, "Hiragino Sans GB.ttc");
  appendAppleFontCandidate(candidates, "ヒラギノ角ゴシック W3.ttc");
  appendAppleFontCandidate(candidates, "ヒラギノ角ゴシック W6.ttc");
  appendAppleFontCandidate(candidates, "SFNS.ttf");
  appendAppleFontCandidate(candidates, "Helvetica.ttc");
  appendAppleFontCandidate(candidates, "HelveticaNeue.ttc");
  appendAppleFontCandidate(candidates, "AppleSDGothicNeo.ttc");
#endif

  if (key == "arial") {
    candidates.emplace_back("/usr/share/fonts/truetype/msttcorefonts/Arial.ttf");
    candidates.emplace_back("/usr/share/fonts/truetype/liberation2/LiberationSans-Regular.ttf");
  }
  if (key.find("dejavu") != std::string::npos || key == "sans" || key == "dejavusans") {
    candidates.emplace_back("/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc");
    candidates.emplace_back("/usr/share/fonts/opentype/noto/NotoSansJP-Regular.otf");
    candidates.emplace_back("/usr/share/fonts/truetype/noto/NotoSansCJK-Regular.ttc");
    candidates.emplace_back("/usr/share/fonts/truetype/noto/NotoSansJP-Regular.ttf");
    candidates.emplace_back("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf");
  }
  if (key.find("serif") != std::string::npos) {
    candidates.emplace_back("/usr/share/fonts/truetype/dejavu/DejaVuSerif.ttf");
  }
  if (key.find("mono") != std::string::npos) {
    candidates.emplace_back("/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf");
  }
  if (key.find("liberation") != std::string::npos) {
    candidates.emplace_back("/usr/share/fonts/truetype/liberation2/LiberationSans-Regular.ttf");
  }

  candidates.emplace_back("/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc");
  candidates.emplace_back("/usr/share/fonts/opentype/noto/NotoSansJP-Regular.otf");
  candidates.emplace_back("/usr/share/fonts/truetype/noto/NotoSansCJK-Regular.ttc");
  candidates.emplace_back("/usr/share/fonts/truetype/noto/NotoSansJP-Regular.ttf");
  candidates.emplace_back("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf");
  candidates.emplace_back("/usr/share/fonts/truetype/liberation2/LiberationSans-Regular.ttf");
  return candidates;
}

std::filesystem::path FontManager::resolveFontPath(const std::string& assetBasePath, const UI_ButtonStyle& style) {
  for (const std::filesystem::path& candidate : fontCandidates(assetBasePath, style.font)) {
    if (std::filesystem::is_regular_file(candidate)) return candidate;
  }
  return {};
}

BLFont FontManager::loadFont(UI_ButtonResources& resources, const UI_ButtonStyle& style) {
  BLFont font;
  const std::filesystem::path path = resolveFontPath(resources.assetBasePath, style);
  if (path.empty()) return font;

  BLFontFace face;
  const std::string key = path.string();
  if (resources.fonts) {
    auto it = resources.fonts->find(key);
    if (it == resources.fonts->end()) {
      if (face.create_from_file(key.c_str()) != BL_SUCCESS) return font;
      it = resources.fonts->emplace(key, face).first;
    }
    face = it->second;
  } else if (face.create_from_file(key.c_str()) != BL_SUCCESS) {
    return font;
  }

  font.create_from_face(face, static_cast<float>(style.fontSize));
  return font;
}

}  // namespace Blend2DUI
