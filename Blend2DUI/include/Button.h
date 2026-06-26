#pragma once

#include <blend2d/blend2d.h>

#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace Blend2DUI {

enum UI_ButtonAction {
  UI_ButtonActionNone,
  UI_ButtonActionMouseHover,
  UI_ButtonActionPressed
};

enum class UI_ButtonContentLayout {
  ImageLeftTextRight,
  ImageAboveText
};

enum class UI_ButtonGradientHoverMode {
  None,
  Cycle
};

struct UI_ButtonResources;
class UI_ShapedTextCache;

struct UI_ButtonContent {
  UI_ButtonContent(std::string_view textValue = {},
                   std::string_view hintValue = {},
                   std::string_view imageValue = {})
      : text(textValue), hint(hintValue), image(imageValue) {}
  UI_ButtonContent(const UI_ButtonContent& other);
  UI_ButtonContent(UI_ButtonContent&& other) noexcept;
  UI_ButtonContent& operator=(const UI_ButtonContent& other);
  UI_ButtonContent& operator=(UI_ButtonContent&& other) noexcept;

  const BLImage* preloadImage(UI_ButtonResources& resources) const;

  std::string text;
  std::string hint;
  std::string image;

 private:
  mutable std::string preparedImageValue_;
  mutable std::string preparedAssetBasePath_;
  mutable const void* preparedImageCache_ = nullptr;
  mutable BLImage preparedImage_;
  mutable bool imagePrepared_ = false;
  mutable bool hasPreparedImage_ = false;
};

struct UI_ButtonStyle {
  UI_ButtonContentLayout layout = UI_ButtonContentLayout::ImageLeftTextRight;
  double corner = 6.0;
  bool hasFill = true;
  uint32_t fillColour = 0xFF505050u;
  bool hasStroke = true;
  uint32_t hoverColour = 0xFF707070u;
  uint32_t pressedColour = 0xFF404040u;
  uint32_t strokeColour = 0xFF101010u;
  double strokeWidth = 1.0;
  uint32_t shadowColour = 0x00000000u;
  double shadowWidth = 0.0;
  uint32_t innerShadowColour = 0x00000000u;
  double innerShadowWidth = 0.0;
  double innerShadowOffsetX = 0.0;
  double innerShadowOffsetY = 0.0;
  std::vector<uint32_t> gradients;
  double gradientAngle = 0.0;
  UI_ButtonGradientHoverMode gradientHover = UI_ButtonGradientHoverMode::None;

  std::string font = "DejaVuSans";
  double fontSize = 14.0;
  bool bold = false;
  bool italic = false;
  uint32_t textColour = 0xFFFFFFFFu;
};

class UI_ButtonStyleDefinition {
 public:
  UI_ButtonStyleDefinition() = default;
  explicit UI_ButtonStyleDefinition(std::string_view styleText);

  const UI_ButtonStyle& style() const { return style_; }

 private:
  static UI_ButtonStyle parseStyle(std::string_view styleText);

  UI_ButtonStyle style_;
};

struct UI_ButtonResources {
  std::unordered_map<std::string, BLImage>* images = nullptr;
  std::unordered_map<std::string, BLFontFace>* fonts = nullptr;
  UI_ShapedTextCache* shapedText = nullptr;
  std::string assetBasePath = ".";
  bool lowPowerMode = false;
};

class Button {
 public:
  Button(std::string id, BLRect rect, const UI_ButtonStyleDefinition& style, const UI_ButtonContent& content = UI_ButtonContent{});

  UI_ButtonAction render(BLContext& ctx,
                         double mouseX,
                         double mouseY,
                         bool mouseDown,
                         bool mousePressed,
                         bool mouseReleased,
                         double seconds,
                         std::string& activeButtonId,
                         std::string& hoveredButtonId,
                         double& hoverStartSeconds,
                         UI_ButtonResources& resources) const;

 private:
  std::string id_;
  BLRect rect_;
  const UI_ButtonStyleDefinition* style_ = nullptr;
  const UI_ButtonContent* content_ = nullptr;
};

}  // namespace Blend2DUI
