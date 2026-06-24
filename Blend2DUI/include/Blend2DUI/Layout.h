#pragma once

#include <blend2d/blend2d.h>

#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace Blend2DUI {

enum class UI_CursorDirection {
  Left,
  Right,
  Top,
  Bottom
};

struct UI_Size {
  double w = 0.0;
  double h = 0.0;
  bool wPercent = false;
  bool hPercent = false;
};

struct UI_RectStyle {
  double padding = 0.0;
  bool hasFill = false;
  uint32_t fillColour = 0x00000000u;
  bool hasStroke = false;
  uint32_t strokeColour = 0x00000000u;
  double strokeWidth = 1.0;
  double corner = 0.0;
  std::vector<uint32_t> gradientFill;
  double gradientAngle = 0.0;
};

class UI_RectStyleDefinition {
 public:
  UI_RectStyleDefinition() = default;
  explicit UI_RectStyleDefinition(std::string_view styleText);

  const UI_RectStyle& style() const { return style_; }

 private:
  static UI_RectStyle parseStyle(std::string_view styleText);

  UI_RectStyle style_;
};

struct UI_RectArea {
  BLRect rect;
  double leftMargin = 0.0;
  double rightMargin = 0.0;
  double topMargin = 0.0;
  double bottomMargin = 0.0;
  double padding = 0.0;
  const UI_RectStyleDefinition* style = nullptr;

  void SetRect(const BLRect& newRect, const UI_RectStyleDefinition& newStyle);
  BLRect GetDrawableArea() const;
};

struct UI_CursorState {
  UI_RectArea area;
  BLRect drawable;
  double startX = 0.0;
  double startY = 0.0;
  double cursorX = 0.0;
  double cursorY = 0.0;
  double gap = 3.0;
  double lineExtent = 0.0;
  UI_CursorDirection direction = UI_CursorDirection::Left;
  bool active = false;
  bool centerNextX = false;
  bool centerNextY = false;
};

UI_Size UI_ParseSize(std::string_view sizeText);

}  // namespace Blend2DUI
