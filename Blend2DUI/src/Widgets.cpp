#include "SceneRenderer.h"

#include "FontManager.h"
#include "Utility.h"
#include "SvgRender/SvgRenderer.h"

#include <algorithm>
#include <cmath>
#include <filesystem>

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

BLRect insetRect(const BLRect& rect, double inset) {
  return BLRect(rect.x + inset,
                rect.y + inset,
                std::max(0.0, rect.w - inset * 2.0),
                std::max(0.0, rect.h - inset * 2.0));
}

uint32_t scaleAlpha(uint32_t colour, double factor) {
  factor = std::max(0.0, std::min(1.0, factor));
  const uint32_t alpha = (colour >> 24) & 0xFFu;
  const uint32_t scaled = static_cast<uint32_t>(std::lround(static_cast<double>(alpha) * factor));
  return (colour & 0x00FFFFFFu) | ((std::min)(scaled, 0xFFu) << 24);
}

void strokeClippedRoundRect(BLContext& ctx,
                            const BLRect& clipRect,
                            const BLRect& rect,
                            double corner,
                            uint32_t colour) {
  if (clipRect.w <= 0.0 || clipRect.h <= 0.0 || (colour >> 24) == 0) return;

  BLContextCookie cookie;
  ctx.save(cookie);
  ctx.clip_to_rect(clipRect);
  ctx.set_stroke_style(BLRgba32(colour));
  ctx.stroke_round_rect(BLRoundRect(rect.x, rect.y, rect.w, rect.h, corner));
  ctx.restore(cookie);
}

bool hasSvgExtension(const std::filesystem::path& path) {
  return lower(path.extension().string()) == ".svg";
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

uint32_t blendColour(uint32_t a, uint32_t b, double t) {
  t = std::max(0.0, std::min(1.0, t));
  const auto blendChannel = [t](uint32_t lhs, uint32_t rhs, int shift) {
    const double av = static_cast<double>((lhs >> shift) & 0xFFu);
    const double bv = static_cast<double>((rhs >> shift) & 0xFFu);
    return static_cast<uint32_t>(std::lround(av + (bv - av) * t)) << shift;
  };
  return blendChannel(a, b, 24) |
         blendChannel(a, b, 16) |
         blendChannel(a, b, 8) |
         blendChannel(a, b, 0);
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
              const BLRect& rect,
              const UI_ButtonStyle& style,
              std::string_view text,
              UI_ButtonResources& resources,
              bool centered) {
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

  const double x = centered
                       ? rect.x + (rect.w - shaped->textMetrics.bounding_box.x1 + shaped->textMetrics.bounding_box.x0) * 0.5
                       : rect.x;
  const double y = rect.y + (rect.h - (shaped->fontMetrics.ascent + shaped->fontMetrics.descent)) * 0.5 +
                   shaped->fontMetrics.ascent;
  ctx.set_fill_style(BLRgba32(style.textColour));
  ctx.fill_glyph_run(BLPoint(x, y), shaped->font, shaped->glyphs.glyph_run());
}

void drawInnerShadow(BLContext& ctx,
                     const BLRect& rect,
                     const UI_ButtonStyle& style,
                     double corner) {
  if (!style.hasFill || style.innerShadowWidth <= 0.0 || (style.innerShadowColour >> 24) == 0) return;

  const int layers = std::max(1, static_cast<int>(std::ceil(style.innerShadowWidth)));
  const double widthFactor = std::max(0.0, std::min(1.0, style.innerShadowWidth));
  const double maxOffset = std::max(std::abs(style.innerShadowOffsetX), std::abs(style.innerShadowOffsetY));
  const bool directional = maxOffset > 0.001;
  ctx.set_stroke_width(1.0);

  for (int i = 0; i < layers; ++i) {
    const double inset = 0.5 + static_cast<double>(i);
    const BLRect shadowRect = insetRect(rect, inset);
    if (shadowRect.w <= 0.0 || shadowRect.h <= 0.0) break;

    const double layerFactor = widthFactor * (1.0 - static_cast<double>(i) / static_cast<double>(layers));
    const uint32_t colour = scaleAlpha(style.innerShadowColour, layerFactor);
    if ((colour >> 24) == 0) continue;

    const double shadowCorner = clampCorner(std::max(0.0, corner - inset), shadowRect);
    if (!directional) {
      ctx.set_stroke_style(BLRgba32(colour));
      ctx.stroke_round_rect(BLRoundRect(shadowRect.x, shadowRect.y, shadowRect.w, shadowRect.h, shadowCorner));
      continue;
    }

    const double clipInset = std::max(1.5, style.innerShadowWidth + maxOffset + static_cast<double>(i) * 0.25);
    const double leftWeight = style.innerShadowOffsetX < 0.0 ? std::min(1.0, -style.innerShadowOffsetX / maxOffset) : 0.0;
    const double rightWeight = style.innerShadowOffsetX > 0.0 ? std::min(1.0, style.innerShadowOffsetX / maxOffset) : 0.0;
    const double topWeight = style.innerShadowOffsetY < 0.0 ? std::min(1.0, -style.innerShadowOffsetY / maxOffset) : 0.0;
    const double bottomWeight = style.innerShadowOffsetY > 0.0 ? std::min(1.0, style.innerShadowOffsetY / maxOffset) : 0.0;

    strokeClippedRoundRect(ctx,
                           BLRect(rect.x, rect.y, rect.w, clipInset),
                           shadowRect,
                           shadowCorner,
                           scaleAlpha(colour, topWeight));
    strokeClippedRoundRect(ctx,
                           BLRect(rect.x, rect.y + rect.h - clipInset, rect.w, clipInset),
                           shadowRect,
                           shadowCorner,
                           scaleAlpha(colour, bottomWeight));
    strokeClippedRoundRect(ctx,
                           BLRect(rect.x, rect.y, clipInset, rect.h),
                           shadowRect,
                           shadowCorner,
                           scaleAlpha(colour, leftWeight));
    strokeClippedRoundRect(ctx,
                           BLRect(rect.x + rect.w - clipInset, rect.y, clipInset, rect.h),
                           shadowRect,
                           shadowCorner,
                           scaleAlpha(colour, rightWeight));
  }
}

void drawButtonLike(BLContext& ctx,
                    const BLRect& rect,
                    const UI_ButtonStyle& style,
                    const UI_ButtonContent& content,
                    UI_ButtonResources& resources,
                    bool hovered,
                    bool active,
                    double hoverElapsed,
                    bool centeredText) {
  const UI_ButtonStyle& renderStyle = style;
  const double corner = clampCorner(renderStyle.corner, rect);
  if (renderStyle.shadowWidth > 0.0 && (renderStyle.shadowColour >> 24) != 0) {
    const double spread = renderStyle.shadowWidth;
    ctx.set_fill_style(BLRgba32(renderStyle.shadowColour));
    ctx.fill_round_rect(BLRoundRect(rect.x - spread * 0.4,
                                    rect.y + spread * 0.35,
                                    rect.w + spread * 0.8,
                                    rect.h + spread * 0.8,
                                    clampCorner(corner + spread * 0.35, rect)));
  }

  uint32_t fill = renderStyle.fillColour;
  if (active) fill = renderStyle.pressedColour;
  else if (hovered) fill = renderStyle.hoverColour;

  if (renderStyle.hasFill) {
    setFillStyle(ctx, renderStyle, rect, fill, hovered, hoverElapsed);
    ctx.fill_round_rect(BLRoundRect(rect.x, rect.y, rect.w, rect.h, corner));
  }

  drawInnerShadow(ctx, rect, renderStyle, corner);

  if (renderStyle.hasStroke && renderStyle.strokeWidth > 0.0) {
    const double strokeInset = renderStyle.strokeWidth * 0.5;
    const BLRect strokeRect = insetRect(rect, strokeInset);
    const double strokeCorner = clampCorner(std::max(0.0, corner - strokeInset), strokeRect);
    ctx.set_stroke_style(BLRgba32(renderStyle.strokeColour));
    ctx.set_stroke_width(renderStyle.strokeWidth);
    ctx.stroke_round_rect(BLRoundRect(strokeRect.x, strokeRect.y, strokeRect.w, strokeRect.h, strokeCorner));
  }

  const BLImage* image = content.preloadImage(resources);
  const double textPadding = 10.0;
  const double imagePadding = 5.0;
  const double imageGap = 8.0;
  const BLRect contentRect = insetRect(rect, textPadding);
  const BLRect imageContentRect = insetRect(rect, imagePadding);

  if (image && !content.text.empty()) {
    if (style.layout == UI_ButtonContentLayout::ImageAboveText) {
      const double imageAreaHeight = std::min(imageContentRect.h, imageContentRect.h * 0.56);
      const BLRect imageArea(imageContentRect.x, imageContentRect.y, imageContentRect.w, imageAreaHeight);
      const BLRect imageRect = fitImageRect(imageArea, *image);
      const double textTop = imageRect.y + imageRect.h + 5.0;
      const BLRect textRect(contentRect.x,
                            textTop,
                            contentRect.w,
                            std::max(0.0, contentRect.y + contentRect.h - textTop));
      ctx.blit_image(imageRect, *image);
      drawText(ctx, textRect, renderStyle, content.text, resources, true);
    } else {
      const double imageSlotWidth = std::min(imageContentRect.h, std::max(16.0, imageContentRect.w * 0.32));
      const BLRect imageArea(imageContentRect.x, imageContentRect.y, imageSlotWidth, imageContentRect.h);
      const BLRect imageRect = fitImageRect(imageArea, *image);
      const double textX = imageArea.x + imageArea.w + imageGap;
      const BLRect textRect(textX,
                            contentRect.y,
                            std::max(0.0, contentRect.x + contentRect.w - textX),
                            contentRect.h);
      ctx.blit_image(imageRect, *image);
      drawText(ctx, textRect, renderStyle, content.text, resources, false);
    }
  } else if (image) {
    const BLRect imageRect = fitImageRect(imageContentRect, *image);
    ctx.blit_image(imageRect, *image);
  } else {
    drawText(ctx, contentRect, renderStyle, content.text, resources, centeredText);
  }
}

void drawTickMark(BLContext& ctx, const BLRect& rect) {
  BLPath tick;
  tick.move_to(rect.x + rect.w * 0.24, rect.y + rect.h * 0.56);
  tick.line_to(rect.x + rect.w * 0.45, rect.y + rect.h * 0.76);
  tick.line_to(rect.x + rect.w * 0.78, rect.y + rect.h * 0.28);
  ctx.set_stroke_style(BLRgba32(0xFFFFFFFFu));
  ctx.set_stroke_width(std::max(1.6, rect.w * 0.12));
  ctx.set_stroke_start_cap(BL_STROKE_CAP_ROUND);
  ctx.set_stroke_end_cap(BL_STROKE_CAP_ROUND);
  ctx.stroke_path(tick);
}

}  // namespace

bool SceneRenderer::UI_TickBox(const std::string& id,
                               const BLRect& rect,
                               bool& checked,
                               const UI_ButtonStyleDefinition& styleDef,
                               const UI_ButtonContent& content) {
  if (!frameActive_) return false;
  if (pointerCapturedByModal(id)) return false;

  const std::string hitId = id + ".tickbox";
  const bool hovered = contains(rect, mouseX_, mouseY_);
  if (hovered && mousePressed_) {
    activeButtonId_ = hitId;
  }
  const bool active = activeButtonId_ == hitId && mouseDown_;
  const bool clicked = hovered && activeButtonId_ == hitId && mouseReleased_;
  if (activeButtonId_ == hitId && mouseReleased_) {
    activeButtonId_.clear();
  }
  if (clicked) checked = !checked;

  const UI_ButtonStyle& style = styleDef.style();
  const double boxSize = std::min(22.0, std::max(16.0, rect.h - 4.0));
  const BLRect boxRect(rect.x, rect.y + (rect.h - boxSize) * 0.5, boxSize, boxSize);
  drawButtonLike(context_, boxRect, style, UI_ButtonContent{}, buttonResources_, hovered, active, 0.0, true);

  if (checked) {
    const BLRect inner = insetRect(boxRect, 3.0);
    const double corner = clampCorner(std::max(2.0, style.corner - 2.0), inner);
    context_.set_fill_style(BLRgba32(style.pressedColour));
    context_.fill_round_rect(BLRoundRect(inner.x, inner.y, inner.w, inner.h, corner));
    drawTickMark(context_, inner);
  }

  if (!content.text.empty() || !content.image.empty()) {
    UI_ButtonStyle labelStyle = style;
    labelStyle.hasFill = false;
    labelStyle.hasStroke = false;
    labelStyle.shadowWidth = 0.0;
    const BLRect labelRect(boxRect.x + boxRect.w + 10.0,
                           rect.y,
                           std::max(0.0, rect.w - boxRect.w - 10.0),
                           rect.h);
    drawButtonLike(context_, labelRect, labelStyle, content, buttonResources_, false, false, 0.0, false);
  }

  return clicked;
}

bool SceneRenderer::UI_TickBox(const std::string& id,
                               const std::string& size,
                               bool& checked,
                               const UI_ButtonStyleDefinition& style,
                               const UI_ButtonContent& content) {
  const UI_Size parsed = resolveLayoutSize(size);
  const BLRect rect = layoutNextRect(parsed.w, parsed.h);
  if (!frameActive_) return false;
  if (!cursor_.active) return UI_TickBox(id, rect, checked, style, content);
  if (!layoutRectVisible(rect)) return false;

  const BLRect clip = intersection(rect, cursor_.drawable);
  ScopedClip scoped(context_, clip);
  const double oldMouseX = mouseX_;
  const double oldMouseY = mouseY_;
  if (!layoutMouseInside()) {
    mouseX_ = -1000000.0;
    mouseY_ = -1000000.0;
  }
  const bool result = UI_TickBox(id, rect, checked, style, content);
  mouseX_ = oldMouseX;
  mouseY_ = oldMouseY;
  return result;
}

bool SceneRenderer::UI_Toggle(const std::string& id,
                              const BLRect& rect,
                              bool& enabled,
                              const UI_ButtonStyleDefinition& styleDef,
                              const UI_ButtonContent& content) {
  if (!frameActive_) return false;
  if (pointerCapturedByModal(id)) return false;

  const std::string hitId = id + ".toggle";
  const bool hovered = contains(rect, mouseX_, mouseY_);
  if (hovered && mousePressed_) {
    activeButtonId_ = hitId;
  }
  const bool active = activeButtonId_ == hitId && mouseDown_;
  const bool clicked = hovered && activeButtonId_ == hitId && mouseReleased_;
  if (activeButtonId_ == hitId && mouseReleased_) {
    activeButtonId_.clear();
  }
  if (clicked) enabled = !enabled;

  const UI_ButtonStyle& style = styleDef.style();
  const double trackH = std::min(30.0, std::max(22.0, rect.h - 2.0));
  const double trackW = std::min(54.0, std::max(40.0, trackH * 1.9));
  const BLRect trackRect(rect.x, rect.y + (rect.h - trackH) * 0.5, trackW, trackH);

  UI_ButtonStyle trackStyle = style;
  trackStyle.corner = trackRect.h * 0.5;
  trackStyle.shadowWidth = 0.0;
  if (enabled) {
    trackStyle.fillColour = style.pressedColour;
    trackStyle.hoverColour = blendColour(style.pressedColour, style.hoverColour, 0.35);
    trackStyle.pressedColour = blendColour(style.pressedColour, 0xFF0F172Au, 0.12);
    trackStyle.strokeColour = blendColour(style.strokeColour, style.pressedColour, 0.35);
  } else {
    trackStyle.fillColour = style.fillColour;
    trackStyle.hoverColour = blendColour(style.hoverColour, 0xFFFFFFFFu, 0.18);
    trackStyle.pressedColour = blendColour(style.fillColour, style.hoverColour, 0.45);
  }
  drawButtonLike(context_, trackRect, trackStyle, UI_ButtonContent{}, buttonResources_, hovered, active, 0.0, true);

  const double knobInset = 3.0;
  const double knobD = std::max(10.0, trackRect.h - knobInset * 2.0);
  const double knobX = enabled ? (trackRect.x + trackRect.w - knobInset - knobD) : (trackRect.x + knobInset);
  const double knobY = trackRect.y + knobInset;
  context_.set_fill_style(BLRgba32(0xFFFFFFFFu));
  context_.fill_circle(BLCircle(knobX + knobD * 0.5, knobY + knobD * 0.5, knobD * 0.5));
  context_.set_stroke_style(BLRgba32(blendColour(style.strokeColour, 0xFF0F172Au, 0.2)));
  context_.set_stroke_width(1.0);
  context_.stroke_circle(BLCircle(knobX + knobD * 0.5, knobY + knobD * 0.5, std::max(1.0, knobD * 0.5 - 0.5)));

  if (!content.text.empty() || !content.image.empty()) {
    UI_ButtonStyle labelStyle = style;
    labelStyle.hasFill = false;
    labelStyle.hasStroke = false;
    labelStyle.shadowWidth = 0.0;
    const BLRect labelRect(trackRect.x + trackRect.w + 12.0,
                           rect.y,
                           std::max(0.0, rect.w - trackRect.w - 12.0),
                           rect.h);
    drawButtonLike(context_, labelRect, labelStyle, content, buttonResources_, false, false, 0.0, false);
  }

  return clicked;
}

bool SceneRenderer::UI_Toggle(const std::string& id,
                              const std::string& size,
                              bool& enabled,
                              const UI_ButtonStyleDefinition& style,
                              const UI_ButtonContent& content) {
  const UI_Size parsed = resolveLayoutSize(size);
  const BLRect rect = layoutNextRect(parsed.w, parsed.h);
  if (!frameActive_) return false;
  if (!cursor_.active) return UI_Toggle(id, rect, enabled, style, content);
  if (!layoutRectVisible(rect)) return false;

  const BLRect clip = intersection(rect, cursor_.drawable);
  ScopedClip scoped(context_, clip);
  const double oldMouseX = mouseX_;
  const double oldMouseY = mouseY_;
  if (!layoutMouseInside()) {
    mouseX_ = -1000000.0;
    mouseY_ = -1000000.0;
  }
  const bool result = UI_Toggle(id, rect, enabled, style, content);
  mouseX_ = oldMouseX;
  mouseY_ = oldMouseY;
  return result;
}

void SceneRenderer::UI_Label(const std::string& id,
                             const BLRect& rect,
                             const UI_ButtonStyleDefinition& styleDef,
                             const UI_ButtonContent& content) {
  (void)id;
  if (!frameActive_) return;
  drawButtonLike(context_, rect, styleDef.style(), content, buttonResources_, false, false, 0.0, false);
}

void SceneRenderer::UI_Label(const std::string& id,
                             const std::string& size,
                             const UI_ButtonStyleDefinition& style,
                             const UI_ButtonContent& content) {
  const UI_Size parsed = resolveLayoutSize(size);
  const BLRect rect = layoutNextRect(parsed.w, parsed.h);
  if (!frameActive_) return;
  if (!cursor_.active) {
    UI_Label(id, rect, style, content);
    return;
  }
  if (!layoutRectVisible(rect)) return;

  const BLRect clip = intersection(rect, cursor_.drawable);
  ScopedClip scoped(context_, clip);
  UI_Label(id, rect, style, content);
}

void SceneRenderer::UI_Image(const std::string& id,
                             const BLRect& rect,
                             const UI_ButtonStyleDefinition& styleDef,
                             const UI_ButtonContent& content) {
  (void)id;
  if (!frameActive_) return;
  drawButtonLike(context_, rect, styleDef.style(), content, buttonResources_, false, false, 0.0, true);
}

void SceneRenderer::UI_Image(const std::string& id,
                             const std::string& size,
                             const UI_ButtonStyleDefinition& style,
                             const UI_ButtonContent& content) {
  const UI_Size parsed = resolveLayoutSize(size);
  const BLRect rect = layoutNextRect(parsed.w, parsed.h);
  if (!frameActive_) return;
  if (!cursor_.active) {
    UI_Image(id, rect, style, content);
    return;
  }
  if (!layoutRectVisible(rect)) return;

  const BLRect clip = intersection(rect, cursor_.drawable);
  ScopedClip scoped(context_, clip);
  UI_Image(id, rect, style, content);
}

int SceneRenderer::UI_ContextMenu(const std::string& id,
                                  const BLRect& rect,
                                  const UI_ButtonStyleDefinition& triggerStyle,
                                  const UI_ButtonContent& triggerContent,
                                  const std::vector<UI_ContextMenuItem>& items,
                                  const UI_ButtonStyleDefinition& menuStyleDef,
                                  const UI_ButtonStyleDefinition& itemStyleDef,
                                  const UI_ContextMenuOptions& options) {
  if (!frameActive_) return -1;
  if (pointerCapturedByModal(id)) return -1;

  const UI_ButtonAction triggerAction = UI_Button(id + ".trigger", rect, triggerStyle, triggerContent);
  UI_ContextMenuState& state = contextMenuStates_[id];
  const bool triggerHovered = contains(rect, mouseX_, mouseY_);
  const bool leftTrigger = options.openOnLeftClick && triggerAction == UI_ButtonActionPressed;
  const bool rightTrigger = options.openOnRightClick && triggerHovered && rightMousePressed_;
  bool openedThisFrame = false;

  if (rightTrigger) {
    state.open = true;
    state.x = mouseX_;
    state.y = mouseY_;
    openedThisFrame = true;
  } else if (leftTrigger) {
    if (state.open) {
      state.open = false;
    } else {
      state.open = true;
      state.x = rect.x;
      state.y = rect.y + rect.h + options.popupOffset;
      openedThisFrame = true;
    }
  }

  if (!state.open || items.empty()) return -1;

  const double menuWidth = std::max(options.menuWidth, rect.w);
  const double itemHeight = std::max(24.0, options.itemHeight);
  const double padding = std::max(2.0, options.padding);
  const double menuHeight = padding * 2.0 + itemHeight * static_cast<double>(items.size());
  const double menuX = std::max(6.0, std::min(state.x, std::max(6.0, static_cast<double>(width_) - menuWidth - 6.0)));
  const double menuY = std::max(6.0, std::min(state.y, std::max(6.0, static_cast<double>(height_) - menuHeight - 6.0)));
  const BLRect menuRect(menuX, menuY, menuWidth, menuHeight);

  if (!openedThisFrame &&
      (mousePressed_ || rightMousePressed_) &&
      !contains(menuRect, mouseX_, mouseY_) &&
      !contains(rect, mouseX_, mouseY_)) {
    state.open = false;
    return -1;
  }

  drawButtonLike(context_, menuRect, menuStyleDef.style(), UI_ButtonContent{}, buttonResources_, false, false, 0.0, false);

  int selectedIndex = -1;
  for (size_t i = 0; i < items.size(); ++i) {
    const BLRect itemRect(menuRect.x + padding,
                          menuRect.y + padding + static_cast<double>(i) * itemHeight,
                          menuRect.w - padding * 2.0,
                          itemHeight);
    const std::string itemId = id + ".item." + std::to_string(i);
    const UI_ContextMenuItem& item = items[i];

    const bool hovered = item.enabled && contains(itemRect, mouseX_, mouseY_);
    if (hovered && mousePressed_) {
      activeButtonId_ = itemId;
    }
    const bool active = item.enabled && activeButtonId_ == itemId && mouseDown_;
    const bool clicked = item.enabled && hovered && activeButtonId_ == itemId && mouseReleased_;
    if (activeButtonId_ == itemId && mouseReleased_) {
      activeButtonId_.clear();
    }

    UI_ButtonStyle itemStyle = itemStyleDef.style();
    if (!item.enabled) {
      itemStyle.hoverColour = itemStyle.fillColour;
      itemStyle.pressedColour = itemStyle.fillColour;
      itemStyle.textColour = blendColour(itemStyle.textColour, 0xFF94A3B8u, 0.65);
      itemStyle.strokeColour = blendColour(itemStyle.strokeColour, 0xFFE2E8F0u, 0.35);
    }

    drawButtonLike(context_,
                   itemRect,
                   itemStyle,
                   item.content,
                   buttonResources_,
                   item.enabled && hovered,
                   item.enabled && active,
                   0.0,
                   false);

    if (clicked) {
      selectedIndex = static_cast<int>(i);
      state.open = false;
      break;
    }
  }

  return selectedIndex;
}

int SceneRenderer::UI_ContextMenu(const std::string& id,
                                  const std::string& size,
                                  const UI_ButtonStyleDefinition& triggerStyle,
                                  const UI_ButtonContent& triggerContent,
                                  const std::vector<UI_ContextMenuItem>& items,
                                  const UI_ButtonStyleDefinition& menuStyle,
                                  const UI_ButtonStyleDefinition& itemStyle,
                                  const UI_ContextMenuOptions& options) {
  const UI_Size parsed = resolveLayoutSize(size);
  const BLRect rect = layoutNextRect(parsed.w, parsed.h);
  if (!frameActive_) return -1;
  if (!cursor_.active) return UI_ContextMenu(id, rect, triggerStyle, triggerContent, items, menuStyle, itemStyle, options);

  auto it = contextMenuStates_.find(id);
  const bool menuOpen = it != contextMenuStates_.end() && it->second.open;
  if (!layoutRectVisible(rect) && !menuOpen) return -1;

  const double oldMouseX = mouseX_;
  const double oldMouseY = mouseY_;
  if (!menuOpen && !layoutMouseInside()) {
    mouseX_ = -1000000.0;
    mouseY_ = -1000000.0;
  }
  int result = -1;
  if (!menuOpen) {
    const BLRect clip = intersection(rect, cursor_.drawable);
    ScopedClip scoped(context_, clip);
    result = UI_ContextMenu(id, rect, triggerStyle, triggerContent, items, menuStyle, itemStyle, options);
  } else {
    result = UI_ContextMenu(id, rect, triggerStyle, triggerContent, items, menuStyle, itemStyle, options);
  }
  mouseX_ = oldMouseX;
  mouseY_ = oldMouseY;
  return result;
}

}  // namespace Blend2DUI
