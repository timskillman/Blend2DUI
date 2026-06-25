#include "SceneRenderer.h"
#include "FontManager.h"
#include "ShapedTextCache.h"
#include "Utility.h"
#include "SvgRender/SvgRenderer.h"

#include <algorithm>
#include <cmath>
#include <filesystem>

namespace Blend2DUI {
namespace {

bool hasSvgExtension(const std::filesystem::path& path) {
  return lower(path.extension().string()) == ".svg";
}

BLRect insetRect(const BLRect& rect, double inset) {
  return BLRect(rect.x + inset,
                rect.y + inset,
                std::max(0.0, rect.w - inset * 2.0),
                std::max(0.0, rect.h - inset * 2.0));
}

UI_ButtonGradientHoverMode parseGradientHoverMode(const std::string& value) {
  const std::string mode = lower(unquote(value));
  if (mode == "cycle") return UI_ButtonGradientHoverMode::Cycle;
  return UI_ButtonGradientHoverMode::None;
}

std::vector<uint32_t> parseGradients(const std::string& value) {
  std::vector<uint32_t> colours = parseGradientColours(value);
  if (colours.size() > 10) colours.resize(10);
  return colours;
}

void appendAncestorAssetCandidates(std::vector<std::filesystem::path>& candidates,
                                   const std::filesystem::path& start,
                                   const std::filesystem::path& requested) {
  namespace fs = std::filesystem;

  fs::path cursor = start.empty() ? fs::current_path() : start;
  if (fs::is_regular_file(cursor)) cursor = cursor.parent_path();

  while (!cursor.empty()) {
    candidates.push_back(cursor / requested);
    candidates.push_back(cursor / "assets" / requested);

    const fs::path parent = cursor.parent_path();
    if (parent == cursor) break;
    cursor = parent;
  }
}

std::filesystem::path resolveAssetPath(const std::string& assetBasePath, const std::string& path) {
  namespace fs = std::filesystem;
  fs::path requested(path);
  if (requested.is_absolute() && fs::is_regular_file(requested)) return requested;
  if (fs::is_regular_file(requested)) return requested;

  const fs::path base(assetBasePath.empty() ? "." : assetBasePath);
  std::vector<fs::path> candidates = {
      base / requested,
      base / "assets" / requested,
      fs::current_path() / requested,
      fs::current_path() / "assets" / requested,
      fs::current_path() / "Blend2DUI" / "assets" / requested,
  };

#ifndef _WIN32
  appendAncestorAssetCandidates(candidates, fs::current_path(), requested);
#endif

  for (const fs::path& candidate : candidates) {
    if (fs::is_regular_file(candidate)) return candidate;
  }
  return requested;
}

const BLImage* loadImage(UI_ButtonResources& resources, std::string_view imagePath) {
  if (imagePath.empty() || !resources.images) return nullptr;

  const std::filesystem::path resolved = resolveAssetPath(resources.assetBasePath, std::string(imagePath));
  const std::string key = resolved.string();
  auto it = resources.images->find(key);
  if (it != resources.images->end()) return &it->second;

  BLImage image;
  if (hasSvgExtension(resolved)) {
    SvgRenderOptions svgOptions;
    svgOptions.inputPath = key;
    if (!renderSvgToImage(svgOptions, image)) return nullptr;
  } else if (image.read_from_file(key.c_str()) != BL_SUCCESS) {
    return nullptr;
  }

  return &resources.images->emplace(key, image).first->second;
}

BLRect fitImageRect(const BLRect& bounds, const BLImage& image) {
  if (bounds.w <= 0.0 || bounds.h <= 0.0) return BLRect(bounds.x, bounds.y, 0.0, 0.0);

  const double sourceWidth = std::max(1, image.width());
  const double sourceHeight = std::max(1, image.height());
  const double scale = std::min(bounds.w / sourceWidth, bounds.h / sourceHeight);
  const double width = sourceWidth * scale;
  const double height = sourceHeight * scale;

  return BLRect(bounds.x + (bounds.w - width) * 0.5,
                bounds.y + (bounds.h - height) * 0.5,
                width,
                height);
}

void setFillStyle(BLContext& ctx,
                  const UI_ButtonStyle& style,
                  const BLRect& rect,
                  uint32_t flatColour,
                  bool hovered,
                  double hoverElapsed) {
  if (style.gradients.empty()) {
    ctx.set_fill_style(BLRgba32(flatColour));
    return;
  }

  const double radians = style.gradientAngle * 3.14159265358979323846 / 180.0;
  const double cx = rect.x + rect.w * 0.5;
  const double cy = rect.y + rect.h * 0.5;
  const double radius = std::sqrt(rect.w * rect.w + rect.h * rect.h) * 0.5;
  const double dx = std::cos(radians) * radius;
  const double dy = std::sin(radians) * radius;

  BLGradient gradient(BLLinearGradientValues(cx - dx, cy - dy, cx + dx, cy + dy));
  if (hovered && style.gradientHover == UI_ButtonGradientHoverMode::Cycle) {
    constexpr double kGradientHoverSpeed = 90.0;
    gradient.set_extend_mode(BL_EXTEND_MODE_REPEAT);
    gradient.translate(hoverElapsed * kGradientHoverSpeed, 0.0);
  }

  const double last = static_cast<double>(style.gradients.size() - 1);
  for (size_t i = 0; i < style.gradients.size(); ++i) {
    gradient.add_stop(last <= 0.0 ? 0.0 : static_cast<double>(i) / last, BLRgba32(style.gradients[i]));
  }
  ctx.set_fill_style(gradient);
}

void drawText(BLContext& ctx,
              const BLRect& textRect,
              const UI_ButtonStyle& style,
              std::string_view text,
              UI_ButtonResources& resources) {
  if (text.empty()) return;

  const UI_ShapedText* shaped = resources.shapedText ? resources.shapedText->get(resources, style, text) : nullptr;
  UI_ShapedText fallback;
  if (!shaped) {
    fallback.font = FontManager::loadFont(resources, style);
    if (!fallback.font.is_valid()) return;
    fallback.glyphs.set_utf8_text(text.data(), text.size());
    fallback.font.shape(fallback.glyphs);
    fallback.font.get_text_metrics(fallback.glyphs, fallback.textMetrics);
    fallback.fontMetrics = fallback.font.metrics();
    shaped = &fallback;
  }

  const double textWidth = shaped->textMetrics.bounding_box.x1 - shaped->textMetrics.bounding_box.x0;
  const double textHeight = shaped->fontMetrics.ascent + shaped->fontMetrics.descent;
  const double x = textRect.x + (textRect.w - textWidth) * 0.5 - shaped->textMetrics.bounding_box.x0;
  const double y = textRect.y + (textRect.h - textHeight) * 0.5 + shaped->fontMetrics.ascent;

  ctx.set_fill_style(BLRgba32(style.textColour));
  ctx.fill_glyph_run(BLPoint(x, y), shaped->font, shaped->glyphs.glyph_run());
}

void drawHint(BLContext& ctx,
              const BLRect& buttonRect,
              const UI_ButtonStyle& style,
              std::string_view hint,
              UI_ButtonResources& resources) {
  if (hint.empty()) return;

  UI_ButtonStyle hintStyle = style;
  hintStyle.fontSize = std::max(11.0, style.fontSize - 1.0);
  const UI_ShapedText* shaped = resources.shapedText ? resources.shapedText->get(resources, hintStyle, hint) : nullptr;
  UI_ShapedText fallback;
  if (!shaped) {
    fallback.font = FontManager::loadFont(resources, hintStyle);
    if (!fallback.font.is_valid()) return;
    fallback.glyphs.set_utf8_text(hint.data(), hint.size());
    fallback.font.shape(fallback.glyphs);
    fallback.font.get_text_metrics(fallback.glyphs, fallback.textMetrics);
    fallback.fontMetrics = fallback.font.metrics();
    shaped = &fallback;
  }

  const double textWidth = shaped->textMetrics.bounding_box.x1 - shaped->textMetrics.bounding_box.x0;
  const double textHeight = shaped->fontMetrics.ascent + shaped->fontMetrics.descent;
  const double boxW = textWidth + 18.0;
  const double boxH = textHeight + 12.0;
  double x = buttonRect.x + (buttonRect.w - boxW) * 0.5;
  double y = buttonRect.y - boxH - 8.0;
  if (y < 6.0) y = buttonRect.y + buttonRect.h + 8.0;
  x = std::max(6.0, x);

  ctx.set_fill_style(BLRgba32(0xEE111827u));
  ctx.fill_round_rect(BLRoundRect(x, y, boxW, boxH, 5.0));
  ctx.set_fill_style(BLRgba32(0xFFFFFFFFu));
  ctx.fill_glyph_run(BLPoint(x + 9.0 - shaped->textMetrics.bounding_box.x0, y + 6.0 + shaped->fontMetrics.ascent),
                     shaped->font,
                     shaped->glyphs.glyph_run());
}

}  // namespace

const BLImage* UI_ButtonContent::preloadImage(UI_ButtonResources& resources) const {
  const bool sameImageRequest = imagePrepared_ &&
                                preparedImageValue_ == image &&
                                preparedAssetBasePath_ == resources.assetBasePath &&
                                preparedImageCache_ == resources.images;
  if (sameImageRequest) {
    return hasPreparedImage_ ? &preparedImage_ : nullptr;
  }

  preparedImageValue_ = image;
  preparedAssetBasePath_ = resources.assetBasePath;
  preparedImageCache_ = resources.images;
  imagePrepared_ = true;
  hasPreparedImage_ = false;
  preparedImage_.reset();

  if (image.empty()) return nullptr;

  const BLImage* cachedImage = loadImage(resources, image);
  if (!cachedImage) return nullptr;

  preparedImage_ = *cachedImage;
  hasPreparedImage_ = true;
  return &preparedImage_;
}

Button::Button(std::string id, BLRect rect, const UI_ButtonStyleDefinition& style, const UI_ButtonContent& content)
    : id_(std::move(id)), rect_(rect), style_(&style), content_(&content) {}

UI_ButtonAction SceneRenderer::UI_Button(const std::string& id,
                                              const BLRect& rect,
                                              const UI_ButtonStyleDefinition& style,
                                              const UI_ButtonContent& content) {
  if (!frameActive_) return UI_ButtonActionNone;
  if (pointerCapturedByModal(id)) return UI_ButtonActionNone;

  Button button(id, rect, style, content);
  return button.render(context_,
                       mouseX_,
                       mouseY_,
                       mouseDown_,
                       mousePressed_,
                       mouseReleased_,
                       frameSeconds_,
                       activeButtonId_,
                       hoveredButtonId_,
                       hoverStartSeconds_,
                       buttonResources_);
}

UI_ButtonStyleDefinition::UI_ButtonStyleDefinition(std::string_view styleText)
    : style_(parseStyle(styleText)) {}

UI_ButtonStyle UI_ButtonStyleDefinition::parseStyle(std::string_view styleText) {
  UI_ButtonStyle style;

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

    if (key == "layout") {
      const std::string layout = lower(unquote(value));
      style.layout = (layout == "under" || layout == "below" || layout == "imageabovetext" || layout == "image_above_text")
                         ? UI_ButtonContentLayout::ImageAboveText
                         : UI_ButtonContentLayout::ImageLeftTextRight;
    } else if (key == "corner") style.corner = parseDouble(value, style.corner);
    else if (key == "fillcolour" || key == "fillcolor") style.fillColour = parseColour(value, style.fillColour);
    else if (key == "hovercolour" || key == "hovercolor") style.hoverColour = parseColour(value, style.hoverColour);
    else if (key == "pressedcolour" || key == "pressedcolor") style.pressedColour = parseColour(value, style.pressedColour);
    else if (key == "strokecolour" || key == "strokecolor" || key == "outlinecolour" || key == "outlinecolor") style.strokeColour = parseColour(value, style.strokeColour);
    else if (key == "strokewidth" || key == "outlinethickness") style.strokeWidth = parseDouble(value, style.strokeWidth);
    else if (key == "shadowcolour" || key == "shadowcolor") style.shadowColour = parseColour(value, style.shadowColour);
    else if (key == "shadowwidth" || key == "shadowspread") style.shadowWidth = parseDouble(value, style.shadowWidth);
    else if (key == "gradients" || key == "gradient") style.gradients = parseGradients(rawPart);
    else if (key == "gradientangle") style.gradientAngle = parseDouble(value, style.gradientAngle);
    else if (key == "gradienthover") style.gradientHover = parseGradientHoverMode(value);
    else if (key == "font") style.font = unquote(value);
    else if (key == "fontsize") style.fontSize = parseDouble(value, style.fontSize);
    else if (key == "fontstyle") {
      const std::string fontStyle = lower(unquote(value));
      style.bold = fontStyle.find("bold") != std::string::npos;
      style.italic = fontStyle.find("italic") != std::string::npos;
    } else if (key == "textcolour" || key == "textcolor") {
      style.textColour = parseColour(value, style.textColour);
    }
  }

  style.corner = std::max(0.0, style.corner);
  style.strokeWidth = std::max(0.0, style.strokeWidth);
  style.shadowWidth = std::max(0.0, style.shadowWidth);
  style.fontSize = std::max(6.0, style.fontSize);
  return style;
}

UI_ButtonAction Button::render(BLContext& ctx,
                               double mouseX,
                               double mouseY,
                               bool mouseDown,
                               bool mousePressed,
                               bool mouseReleased,
                               double seconds,
                               std::string& activeButtonId,
                               std::string& hoveredButtonId,
                               double& hoverStartSeconds,
                               UI_ButtonResources& resources) const {
  const UI_ButtonStyle& style = style_->style();
  const UI_ButtonContent& content = *content_;
  const bool hovered = contains(rect_, mouseX, mouseY);

  if (hoveredButtonId != id_) {
    if (hovered) {
      hoveredButtonId = id_;
      hoverStartSeconds = seconds;
    }
  } else if (!hovered) {
    hoveredButtonId.clear();
  }

  if (hovered && mousePressed) {
    activeButtonId = id_;
  }

  const bool active = activeButtonId == id_ && mouseDown;
  const bool clicked = hovered && activeButtonId == id_ && mouseReleased;
  if (activeButtonId == id_ && mouseReleased) {
    activeButtonId.clear();
  }

  const double hoverElapsed = hovered ? std::max(0.0, seconds - hoverStartSeconds) : 0.0;

  const double corner = clampCorner(style.corner, rect_);
  if (style.shadowWidth > 0.0 && (style.shadowColour >> 24) != 0) {
    const double spread = style.shadowWidth;
    ctx.set_fill_style(BLRgba32(style.shadowColour));
    ctx.fill_round_rect(BLRoundRect(rect_.x - spread * 0.4, rect_.y + spread * 0.35,
                                    rect_.w + spread * 0.8, rect_.h + spread * 0.8,
                                    clampCorner(corner + spread * 0.35, rect_)));
  }

  uint32_t fill = style.fillColour;
  if (active) fill = style.pressedColour;
  else if (hovered) fill = style.hoverColour;

  setFillStyle(ctx, style, rect_, fill, hovered, hoverElapsed);
  ctx.fill_round_rect(BLRoundRect(rect_.x, rect_.y, rect_.w, rect_.h, corner));

  if (style.strokeWidth > 0.0) {
    const double strokeInset = style.strokeWidth * 0.5;
    const BLRect strokeRect = insetRect(rect_, strokeInset);
    const double strokeCorner = clampCorner(std::max(0.0, corner - strokeInset), strokeRect);
    ctx.set_stroke_style(BLRgba32(style.strokeColour));
    ctx.set_stroke_width(style.strokeWidth);
    ctx.stroke_round_rect(BLRoundRect(strokeRect.x, strokeRect.y, strokeRect.w, strokeRect.h, strokeCorner));
  }

  const BLImage* image = content.preloadImage(resources);
  const double textPadding = 10.0;
  const double imagePadding = 5.0;
  const double imageGap = 8.0;
  const BLRect contentRect = insetRect(rect_, textPadding);
  const BLRect imageContentRect = insetRect(rect_, imagePadding);

  if (image && !content.text.empty()) {
    if (style.layout == UI_ButtonContentLayout::ImageAboveText) {
      const double imageAreaHeight = std::min(imageContentRect.h, imageContentRect.h * 0.56);
      const BLRect imageArea(imageContentRect.x, imageContentRect.y, imageContentRect.w, imageAreaHeight);
      const BLRect imageRect = fitImageRect(imageArea, *image);
      const double textTop = imageRect.y + imageRect.h + 5.0;
      BLRect textRect(contentRect.x,
                      textTop,
                      contentRect.w,
                      std::max(0.0, contentRect.y + contentRect.h - textTop));
      ctx.blit_image(imageRect, *image);
      drawText(ctx, textRect, style, content.text, resources);
    } else {
      const double imageSlotWidth = std::min(imageContentRect.h, std::max(16.0, imageContentRect.w * 0.32));
      const BLRect imageArea(imageContentRect.x, imageContentRect.y, imageSlotWidth, imageContentRect.h);
      const BLRect imageRect = fitImageRect(imageArea, *image);
      const double textX = imageArea.x + imageArea.w + imageGap;
      BLRect textRect(textX,
                      contentRect.y,
                      std::max(0.0, contentRect.x + contentRect.w - textX),
                      contentRect.h);
      ctx.blit_image(imageRect, *image);
      drawText(ctx, textRect, style, content.text, resources);
    }
  } else if (image) {
    const BLRect imageRect = fitImageRect(imageContentRect, *image);
    ctx.blit_image(imageRect, *image);
  } else {
    drawText(ctx, contentRect, style, content.text, resources);
  }

  if (hovered && !content.hint.empty() && seconds - hoverStartSeconds >= 2.0) {
    drawHint(ctx, rect_, style, content.hint, resources);
  }

  if (clicked) return UI_ButtonActionPressed;
  if (hovered) return UI_ButtonActionMouseHover;
  return UI_ButtonActionNone;
}

}  // namespace Blend2DUI
