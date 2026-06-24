#include "Blend2DUI/SdlBlend2DRenderer.h"
#include "Blend2DUI/Utility.h"

#include <algorithm>
#include <cmath>

namespace Blend2DUI {
namespace {

class ScopedClip {
 public:
  ScopedClip(BLContext& ctx, const BLRect& clip) : ctx_(ctx) {
    ctx_.save(cookie_);
    saved_ = true;
    ctx_.clip_to_rect(clip);
  }

  ~ScopedClip() {
    if (saved_) ctx_.restore(cookie_);
  }

 private:
  BLContext& ctx_;
  BLContextCookie cookie_;
  bool saved_ = false;
};

void drawArea(BLContext& ctx, const UI_RectArea& area) {
  if (!area.style) return;
  const UI_RectStyle& style = area.style->style();
  if (style.hasFill || !style.gradientFill.empty()) {
    if (!style.gradientFill.empty()) {
      const double radians = style.gradientAngle * 3.14159265358979323846 / 180.0;
      const double cx = area.rect.x + area.rect.w * 0.5;
      const double cy = area.rect.y + area.rect.h * 0.5;
      const double radius = std::sqrt(area.rect.w * area.rect.w + area.rect.h * area.rect.h) * 0.5;
      BLGradient gradient(BLLinearGradientValues(cx - std::cos(radians) * radius,
                                                 cy - std::sin(radians) * radius,
                                                 cx + std::cos(radians) * radius,
                                                 cy + std::sin(radians) * radius));
      const double last = static_cast<double>(style.gradientFill.size() - 1);
      for (size_t i = 0; i < style.gradientFill.size(); ++i) {
        gradient.add_stop(last <= 0.0 ? 0.0 : static_cast<double>(i) / last, BLRgba32(style.gradientFill[i]));
      }
      ctx.set_fill_style(gradient);
    } else {
      ctx.set_fill_style(BLRgba32(style.fillColour));
    }
    ctx.fill_round_rect(BLRoundRect(area.rect.x, area.rect.y, area.rect.w, area.rect.h, style.corner));
  }
  if (style.hasStroke) {
    ctx.set_stroke_style(BLRgba32(style.strokeColour));
    ctx.set_stroke_width(std::max(0.0, style.strokeWidth));
    const double inset = style.strokeWidth * 0.5;
    ctx.stroke_round_rect(BLRoundRect(area.rect.x + inset,
                                      area.rect.y + inset,
                                      std::max(0.0, area.rect.w - style.strokeWidth),
                                      std::max(0.0, area.rect.h - style.strokeWidth),
                                      style.corner));
  }
}

}  // namespace

UI_RectStyleDefinition::UI_RectStyleDefinition(std::string_view styleText)
    : style_(parseStyle(styleText)) {}

UI_RectStyle UI_RectStyleDefinition::parseStyle(std::string_view styleText) {
  UI_RectStyle style;
  for (const std::string& rawPart : splitTopLevel(std::string(styleText))) {
    const size_t colon = rawPart.find(':');
    const size_t bracket = rawPart.find('[');
    std::string key;
    std::string value;
    if (bracket != std::string::npos && (colon == std::string::npos || bracket < colon)) {
      key = rawPart.substr(0, bracket);
      value = rawPart.substr(bracket);
    } else if (colon != std::string::npos) {
      key = rawPart.substr(0, colon);
      value = rawPart.substr(colon + 1);
    } else {
      continue;
    }

    key = lower(trim(key));
    value = trim(value);
    if (key == "padding") style.padding = std::max(0.0, parseDouble(value, style.padding));
    else if (key == "rectfill" || key == "fill") {
      style.fillColour = parseColour(value, style.fillColour);
      style.hasFill = true;
    } else if (key == "rectstroke" || key == "stroke") {
      style.strokeColour = parseColour(value, style.strokeColour);
      style.hasStroke = true;
    } else if (key == "rectstrokewidth" || key == "strokewidth") style.strokeWidth = std::max(0.0, parseDouble(value, style.strokeWidth));
    else if (key == "rectcorner" || key == "corner") style.corner = std::max(0.0, parseDouble(value, style.corner));
    else if (key == "rectgradientfill" || key == "gradientfill") style.gradientFill = parseGradientColours(rawPart);
    else if (key == "rectgradientangle" || key == "gradientangle") style.gradientAngle = parseDouble(value, style.gradientAngle);
  }
  return style;
}

void UI_RectArea::SetRect(const BLRect& newRect, const UI_RectStyleDefinition& newStyle) {
  rect = newRect;
  style = &newStyle;
  padding = newStyle.style().padding;
}

BLRect UI_RectArea::GetDrawableArea() const {
  const double x = rect.x + leftMargin + padding;
  const double y = rect.y + topMargin + padding;
  const double w = rect.w - leftMargin - rightMargin - padding * 2.0;
  const double h = rect.h - topMargin - bottomMargin - padding * 2.0;
  return BLRect(x, y, std::max(0.0, w), std::max(0.0, h));
}

UI_Size UI_ParseSize(std::string_view sizeText) {
  std::string text(sizeText);
  const size_t x = lower(text).find('x');
  if (x == std::string::npos) return {};
  auto parsePart = [](std::string part, bool& percent) {
    part = trim(part);
    percent = !part.empty() && part.back() == '%';
    if (percent) part.pop_back();
    return parseDouble(part, 0.0);
  };

  UI_Size size;
  size.w = parsePart(text.substr(0, x), size.wPercent);
  size.h = parsePart(text.substr(x + 1), size.hPercent);
  return size;
}

void SdlBlend2DRenderer::UI_CursorRect(const UI_RectArea& area) {
  cursor_ = UI_CursorState();
  cursor_.area = area;
  cursor_.drawable = area.GetDrawableArea();
  cursor_.direction = UI_CursorDirection::Left;
  cursor_.gap = 3.0;
  cursor_.active = true;
  cursor_.startX = cursor_.drawable.x;
  cursor_.startY = cursor_.drawable.y;
  cursor_.cursorX = cursor_.startX;
  cursor_.cursorY = cursor_.startY;
  if (frameActive_) drawArea(context_, area);
}

void SdlBlend2DRenderer::UI_CursorSave(const std::string& id) {
  savedCursors_[id] = cursor_;
}

void SdlBlend2DRenderer::UI_CursorUse(const std::string& id) {
  auto it = savedCursors_.find(id);
  if (it != savedCursors_.end()) cursor_ = it->second;
}

void SdlBlend2DRenderer::UI_CursorOffset(double x, double y) {
  cursor_.cursorX += x;
  cursor_.cursorY += y;
}

void SdlBlend2DRenderer::UI_CursorNext() {
  if (!cursor_.active) return;
  if (cursor_.direction == UI_CursorDirection::Left || cursor_.direction == UI_CursorDirection::Right) {
    cursor_.cursorY += cursor_.lineExtent + cursor_.gap;
    cursor_.cursorX = cursor_.direction == UI_CursorDirection::Right ? cursor_.drawable.x + cursor_.drawable.w : cursor_.drawable.x;
  } else {
    cursor_.cursorX += cursor_.lineExtent + cursor_.gap;
    cursor_.cursorY = cursor_.direction == UI_CursorDirection::Bottom ? cursor_.drawable.y + cursor_.drawable.h : cursor_.drawable.y;
  }
  cursor_.lineExtent = 0.0;
}

void SdlBlend2DRenderer::UI_CursorLeft(int gap) {
  cursor_.direction = UI_CursorDirection::Left;
  cursor_.gap = static_cast<double>(std::max(0, gap));
  cursor_.cursorX = cursor_.drawable.x;
}

void SdlBlend2DRenderer::UI_CursorRight(int gap) {
  cursor_.direction = UI_CursorDirection::Right;
  cursor_.gap = static_cast<double>(std::max(0, gap));
  cursor_.cursorX = cursor_.drawable.x + cursor_.drawable.w;
}

void SdlBlend2DRenderer::UI_CursorBottom(int gap) {
  cursor_.direction = UI_CursorDirection::Bottom;
  cursor_.gap = static_cast<double>(std::max(0, gap));
  cursor_.cursorY = cursor_.drawable.y + cursor_.drawable.h;
}

void SdlBlend2DRenderer::UI_CursorTop(int gap) {
  cursor_.direction = UI_CursorDirection::Top;
  cursor_.gap = static_cast<double>(std::max(0, gap));
  cursor_.cursorY = cursor_.drawable.y;
}

void SdlBlend2DRenderer::UI_CursorVerticalCenter() {
  cursor_.centerNextY = true;
}

void SdlBlend2DRenderer::UI_CursorHorizontalCenter() {
  cursor_.centerNextX = true;
}

void SdlBlend2DRenderer::UI_CursorLine() {
  if (!cursor_.active || !frameActive_) return;
  const double offset = cursor_.gap;
  context_.set_stroke_style(BLRgba32(0x66708391u));
  context_.set_stroke_width(1.0);
  if (cursor_.direction == UI_CursorDirection::Left) {
    const double x = cursor_.cursorX + offset;
    context_.stroke_line(BLLine(x, cursor_.drawable.y, x, cursor_.drawable.y + cursor_.drawable.h));
    cursor_.cursorX += cursor_.gap * 2.0;
  } else if (cursor_.direction == UI_CursorDirection::Right) {
    const double x = cursor_.cursorX - offset;
    context_.stroke_line(BLLine(x, cursor_.drawable.y, x, cursor_.drawable.y + cursor_.drawable.h));
    cursor_.cursorX -= cursor_.gap * 2.0;
  } else if (cursor_.direction == UI_CursorDirection::Top) {
    const double y = cursor_.cursorY + offset;
    context_.stroke_line(BLLine(cursor_.drawable.x, y, cursor_.drawable.x + cursor_.drawable.w, y));
    cursor_.cursorY += cursor_.gap * 2.0;
  } else {
    const double y = cursor_.cursorY - offset;
    context_.stroke_line(BLLine(cursor_.drawable.x, y, cursor_.drawable.x + cursor_.drawable.w, y));
    cursor_.cursorY -= cursor_.gap * 2.0;
  }
}

void SdlBlend2DRenderer::UI_CursorGap(int gap) {
  const double amount = static_cast<double>(std::max(0, gap));
  if (cursor_.direction == UI_CursorDirection::Left) cursor_.cursorX += amount;
  else if (cursor_.direction == UI_CursorDirection::Right) cursor_.cursorX -= amount;
  else if (cursor_.direction == UI_CursorDirection::Top) cursor_.cursorY += amount;
  else cursor_.cursorY -= amount;
}

UI_Size SdlBlend2DRenderer::resolveLayoutSize(const std::string& size) const {
  UI_Size parsed = UI_ParseSize(size);
  if (cursor_.active) {
    if (parsed.wPercent) parsed.w = cursor_.drawable.w * parsed.w / 100.0;
    if (parsed.hPercent) parsed.h = cursor_.drawable.h * parsed.h / 100.0;
  }
  parsed.w = std::max(0.0, parsed.w);
  parsed.h = std::max(0.0, parsed.h);
  return parsed;
}

BLRect SdlBlend2DRenderer::layoutNextRect(double width, double height) {
  if (!cursor_.active) return BLRect(0, 0, width, height);

  BLRect rect;
  if (cursor_.direction == UI_CursorDirection::Left) {
    rect = BLRect(cursor_.cursorX, cursor_.cursorY, width, height);
    cursor_.cursorX += width + cursor_.gap;
    cursor_.lineExtent = std::max(cursor_.lineExtent, height);
  } else if (cursor_.direction == UI_CursorDirection::Right) {
    rect = BLRect(cursor_.cursorX - width, cursor_.cursorY, width, height);
    cursor_.cursorX -= width + cursor_.gap;
    cursor_.lineExtent = std::max(cursor_.lineExtent, height);
  } else if (cursor_.direction == UI_CursorDirection::Top) {
    rect = BLRect(cursor_.cursorX, cursor_.cursorY, width, height);
    cursor_.cursorY += height + cursor_.gap;
    cursor_.lineExtent = std::max(cursor_.lineExtent, width);
  } else {
    rect = BLRect(cursor_.cursorX, cursor_.cursorY - height, width, height);
    cursor_.cursorY -= height + cursor_.gap;
    cursor_.lineExtent = std::max(cursor_.lineExtent, width);
  }

  if (cursor_.centerNextX) {
    rect.x = cursor_.drawable.x + (cursor_.drawable.w - width) * 0.5;
    cursor_.centerNextX = false;
  }
  if (cursor_.centerNextY) {
    rect.y = cursor_.drawable.y + (cursor_.drawable.h - height) * 0.5;
    cursor_.centerNextY = false;
  }
  return rect;
}

bool SdlBlend2DRenderer::layoutRectVisible(const BLRect& rect) const {
  return !cursor_.active || intersects(rect, cursor_.drawable);
}

bool SdlBlend2DRenderer::layoutMouseInside() const {
  return !cursor_.active || contains(cursor_.drawable, mouseX_, mouseY_);
}

UI_ButtonAction SdlBlend2DRenderer::UI_Button(const std::string& id,
                                              const std::string& size,
                                              const UI_ButtonStyleDefinition& style,
                                              const UI_ButtonContent& content) {
  const UI_Size parsed = resolveLayoutSize(size);
  const BLRect rect = layoutNextRect(parsed.w, parsed.h);
  if (!frameActive_) return UI_ButtonActionNone;
  if (!cursor_.active) return UI_Button(id, rect, style, content);
  if (!layoutRectVisible(rect)) return UI_ButtonActionNone;
  const BLRect clip = intersection(rect, cursor_.drawable);
  ScopedClip scoped(context_, clip);
  const double oldMouseX = mouseX_;
  const double oldMouseY = mouseY_;
  if (!layoutMouseInside()) {
    mouseX_ = -1000000.0;
    mouseY_ = -1000000.0;
  }
  const UI_ButtonAction result = UI_Button(id, rect, style, content);
  mouseX_ = oldMouseX;
  mouseY_ = oldMouseY;
  return result;
}

bool SdlBlend2DRenderer::UI_TextInput(const std::string& id,
                                      const std::string& size,
                                      const UI_TextInputOptions& options,
                                      std::string& text,
                                      const UI_ButtonStyleDefinition& style) {
  const UI_Size parsed = resolveLayoutSize(size);
  const BLRect rect = layoutNextRect(parsed.w, parsed.h);
  if (!frameActive_) return false;
  if (!cursor_.active) return UI_TextInput(id, rect, options, text, style);
  if (!layoutRectVisible(rect)) return false;
  const BLRect clip = intersection(rect, cursor_.drawable);
  ScopedClip scoped(context_, clip);
  const double oldMouseX = mouseX_;
  const double oldMouseY = mouseY_;
  if (!layoutMouseInside()) {
    mouseX_ = -1000000.0;
    mouseY_ = -1000000.0;
  }
  const bool result = UI_TextInput(id, rect, options, text, style);
  mouseX_ = oldMouseX;
  mouseY_ = oldMouseY;
  return result;
}

bool SdlBlend2DRenderer::UI_Slider(const std::string& id,
                                   const std::string& size,
                                   const UI_SliderOptions& options,
                                   double& value,
                                   const UI_ButtonStyleDefinition& style) {
  const UI_Size parsed = resolveLayoutSize(size);
  const BLRect rect = layoutNextRect(parsed.w, parsed.h);
  if (!frameActive_) return false;
  if (!cursor_.active) return UI_Slider(id, rect, options, value, style);
  if (!layoutRectVisible(rect)) return false;
  const BLRect clip = intersection(rect, cursor_.drawable);
  ScopedClip scoped(context_, clip);
  const double oldMouseX = mouseX_;
  const double oldMouseY = mouseY_;
  if (!layoutMouseInside()) {
    mouseX_ = -1000000.0;
    mouseY_ = -1000000.0;
  }
  const bool result = UI_Slider(id, rect, options, value, style);
  mouseX_ = oldMouseX;
  mouseY_ = oldMouseY;
  return result;
}

}  // namespace Blend2DUI
