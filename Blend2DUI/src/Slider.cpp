#include "SceneRenderer.h"
#include "FontManager.h"
#include "ShapedTextCache.h"
#include "Utility.h"
#include "SvgRender/SvgRenderer.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <iomanip>
#include <sstream>

namespace Blend2DUI {
namespace {

constexpr double kControlGap = 8.0;
constexpr double kHeadingHeight = 22.0;
constexpr double kValueWidth = 76.0;
constexpr double kButtonSize = 22.0;
constexpr double kArrowGap = 4.0;
constexpr double kThumbSize = 18.0;

double clampValue(double value, double minValue, double maxValue) {
  if (minValue > maxValue) std::swap(minValue, maxValue);
  return std::max(minValue, std::min(maxValue, value));
}

double snapValue(double value, const UI_SliderOptions& options) {
  const double minValue = std::min(options.minValue, options.maxValue);
  const double maxValue = std::max(options.minValue, options.maxValue);
  value = clampValue(value, minValue, maxValue);
  const double step = std::abs(options.step);
  if (step > 0.0) {
    value = minValue + std::round((value - minValue) / step) * step;
  }
  if (options.integer) value = std::round(value);
  return clampValue(value, minValue, maxValue);
}

double valueRatio(double value, const UI_SliderOptions& options) {
  const double span = options.maxValue - options.minValue;
  if (std::abs(span) < 0.0000001) return 0.0;
  return clampValue((value - options.minValue) / span, 0.0, 1.0);
}

bool hasSvgExtension(const std::filesystem::path& path) {
  return lower(path.extension().string()) == ".svg";
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

  for (const fs::path& candidate : candidates) {
    if (fs::is_regular_file(candidate)) return candidate;
  }
  return requested;
}

const BLImage* loadImage(UI_ButtonResources& resources, const std::string& imagePath) {
  if (imagePath.empty() || !resources.images) return nullptr;

  const std::filesystem::path resolved = resolveAssetPath(resources.assetBasePath, imagePath);
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

void drawImageInRect(BLContext& ctx, const BLImage* image, const BLRect& rect) {
  if (!image || rect.w <= 0.0 || rect.h <= 0.0) return;
  ctx.blit_image(rect, *image, BLRectI(0, 0, image->width(), image->height()));
}

void drawText(BLContext& ctx,
              const BLRect& rect,
              const UI_ButtonStyle& style,
              std::string_view text,
              UI_ButtonResources& resources,
              uint32_t colour,
              bool centered = false) {
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
  ctx.set_fill_style(BLRgba32(colour));
  ctx.fill_glyph_run(BLPoint(x, y), shaped->font, shaped->glyphs.glyph_run());
}

std::string formatValue(double value, const UI_SliderOptions& options) {
  std::ostringstream out;
  if (options.integer) {
    out << static_cast<long long>(std::llround(value));
  } else {
    out << std::fixed << std::setprecision(std::abs(options.step) >= 1.0 ? 1 : 2) << value;
    std::string text = out.str();
    while (text.size() > 1 && text.back() == '0') text.pop_back();
    if (!text.empty() && text.back() == '.') text.pop_back();
    return text;
  }
  return out.str();
}

bool parseValue(const std::string& text, double& value) {
  try {
    size_t consumed = 0;
    const double parsed = std::stod(text, &consumed);
    if (consumed == 0) return false;
    value = parsed;
    return true;
  } catch (...) {
    return false;
  }
}

bool acceptsNumericChar(char ch, const std::string& text) {
  if (std::isdigit(static_cast<unsigned char>(ch))) return true;
  if (ch == '.' && text.find('.') == std::string::npos) return true;
  if ((ch == '-' || ch == '+') && text.empty()) return true;
  return false;
}

void drawFallbackTrack(BLContext& ctx, const BLRect& track, const UI_SliderOptions& options, double ratio) {
  const bool horizontal = options.orientation == UI_SliderOrientation::Horizontal;
  const double corner = horizontal ? track.h * 0.5 : track.w * 0.5;
  ctx.set_fill_style(BLRgba32(0xFF1F2937u));
  ctx.fill_round_rect(BLRoundRect(track.x, track.y, track.w, track.h, corner));
  ctx.set_stroke_style(BLRgba32(0xFF0F172Au));
  ctx.set_stroke_width(1.0);
  ctx.stroke_round_rect(BLRoundRect(track.x + 0.5, track.y + 0.5, track.w - 1.0, track.h - 1.0, corner));

  BLRect lit = track;
  if (horizontal) {
    lit.w = std::max(track.h, track.w * ratio);
  } else {
    const double litH = std::max(track.w, track.h * ratio);
    lit.y = track.y + track.h - litH;
    lit.h = litH;
  }
  BLGradient glow(horizontal
                      ? BLLinearGradientValues(lit.x, lit.y, lit.x + lit.w, lit.y)
                      : BLLinearGradientValues(lit.x, lit.y + lit.h, lit.x, lit.y));
  glow.add_stop(0.0, BLRgba32(0xFF0EA5E9u));
  glow.add_stop(0.55, BLRgba32(0xFF38BDF8u));
  glow.add_stop(1.0, BLRgba32(0xFFE0F2FEu));
  ctx.set_fill_style(glow);
  ctx.fill_round_rect(BLRoundRect(lit.x, lit.y, lit.w, lit.h, corner));
}

void drawArtworkTrack(BLContext& ctx,
                      UI_ButtonResources& resources,
                      const BLRect& track,
                      const UI_SliderOptions& options,
                      double ratio) {
  const UI_SliderArtwork& art = options.artwork;
  const bool horizontal = options.orientation == UI_SliderOrientation::Horizontal;
  const double cap = horizontal ? std::min(track.h, track.w * 0.25) : std::min(track.w, track.h * 0.25);
  const BLImage* unlitLeft = loadImage(resources, art.unlitLeftCap);
  const BLImage* unlitMiddle = loadImage(resources, art.unlitMiddle);
  const BLImage* unlitRight = loadImage(resources, art.unlitRightCap);
  const BLImage* litLeft = loadImage(resources, art.litLeftCap);
  const BLImage* litMiddle = loadImage(resources, art.litMiddle);
  const BLImage* litRight = loadImage(resources, art.litRightCap);
  const bool hasUnlit = unlitLeft || unlitMiddle || unlitRight;
  const bool hasLit = litLeft || litMiddle || litRight;
  if (!hasUnlit && !hasLit) {
    drawFallbackTrack(ctx, track, options, ratio);
    return;
  }

  auto drawPieces = [&](const BLImage* left, const BLImage* middle, const BLImage* right) {
    if (horizontal) {
      drawImageInRect(ctx, left, BLRect(track.x, track.y, cap, track.h));
      drawImageInRect(ctx, middle, BLRect(track.x + cap, track.y, std::max(0.0, track.w - cap * 2.0), track.h));
      drawImageInRect(ctx, right, BLRect(track.x + track.w - cap, track.y, cap, track.h));
    } else {
      drawImageInRect(ctx, left, BLRect(track.x, track.y + track.h - cap, track.w, cap));
      drawImageInRect(ctx, middle, BLRect(track.x, track.y + cap, track.w, std::max(0.0, track.h - cap * 2.0)));
      drawImageInRect(ctx, right, BLRect(track.x, track.y, track.w, cap));
    }
  };

  drawPieces(unlitLeft, unlitMiddle, unlitRight);
  BLContextCookie cookie;
  ctx.save(cookie);
  if (horizontal) {
    ctx.clip_to_rect(BLRect(track.x, track.y, track.w * ratio, track.h));
  } else {
    const double litH = track.h * ratio;
    ctx.clip_to_rect(BLRect(track.x, track.y + track.h - litH, track.w, litH));
  }
  drawPieces(litLeft, litMiddle, litRight);
  ctx.restore(cookie);
}

void drawThumb(BLContext& ctx, UI_ButtonResources& resources, const BLRect& rect, const UI_SliderOptions& options) {
  if (const BLImage* image = loadImage(resources, options.artwork.thumb)) {
    drawImageInRect(ctx, image, rect);
    return;
  }

  ctx.set_fill_style(BLRgba32(0xFFE0F2FEu));
  ctx.set_stroke_style(BLRgba32(0xFF0284C7u));
  ctx.set_stroke_width(2.0);
  const double cx = rect.x + rect.w * 0.5;
  const double cy = rect.y + rect.h * 0.5;
  const double r = std::min(rect.w, rect.h) * 0.5;

  if (options.thumbShape == UI_SliderThumbShape::Circle) {
    ctx.fill_circle(BLCircle(cx, cy, r));
    ctx.stroke_circle(BLCircle(cx, cy, r - 1.0));
    return;
  }

  BLPath path;
  if (options.thumbShape == UI_SliderThumbShape::Diamond) {
    path.move_to(cx, rect.y);
    path.line_to(rect.x + rect.w, cy);
    path.line_to(cx, rect.y + rect.h);
    path.line_to(rect.x, cy);
  } else if (options.thumbShape == UI_SliderThumbShape::TriangleDown) {
    path.move_to(rect.x, rect.y);
    path.line_to(rect.x + rect.w, rect.y);
    path.line_to(cx, rect.y + rect.h);
  } else {
    path.move_to(cx, rect.y);
    path.line_to(rect.x + rect.w, rect.y + rect.h);
    path.line_to(rect.x, rect.y + rect.h);
  }
  path.close();
  ctx.fill_path(path);
  ctx.stroke_path(path);
}

BLRect valueEntryRect(const BLRect& rect, const UI_SliderOptions& options) {
  if (!options.showValueEntry) return BLRect();
  if (options.orientation == UI_SliderOrientation::Horizontal) {
    const double arrowReserve = options.showStepButtons ? (kButtonSize * 2.0 + kArrowGap * 2.0) : 0.0;
    const double x = rect.x + rect.w - kValueWidth - arrowReserve;
    return BLRect(x, rect.y + kHeadingHeight, kValueWidth, 28.0);
  }
  const double arrowReserve = options.showStepButtons ? (kButtonSize + kArrowGap) : 0.0;
  return BLRect(rect.x, rect.y + rect.h - 30.0, std::max(28.0, std::min(rect.w - arrowReserve, kValueWidth)), 28.0);
}

BLRect stepButtonRect(const BLRect& rect, const UI_SliderOptions& options, bool increment) {
  if (!options.showStepButtons) return BLRect();
  const BLRect valueRect = valueEntryRect(rect, options);
  if (options.orientation == UI_SliderOrientation::Horizontal) {
    const double y = valueRect.y + (valueRect.h - kButtonSize) * 0.5;
    const double x = increment ? valueRect.x + valueRect.w + kArrowGap : valueRect.x - kButtonSize - kArrowGap;
    return BLRect(x, y, kButtonSize, kButtonSize);
  }
  const double buttonH = (valueRect.h - 2.0) * 0.5;
  return BLRect(valueRect.x + valueRect.w + kArrowGap,
                valueRect.y + (increment ? 0.0 : buttonH + 2.0),
                kButtonSize,
                buttonH);
}

BLRect trackRect(const BLRect& rect, const UI_SliderOptions& options) {
  if (options.orientation == UI_SliderOrientation::Horizontal) {
    const BLRect valueRect = valueEntryRect(rect, options);
    double controlsX = valueRect.x;
    if (options.showStepButtons) {
      controlsX -= kButtonSize + kArrowGap;
    }
    const double controlsW = options.showValueEntry ? std::max(0.0, rect.x + rect.w - controlsX) + kControlGap : 0.0;
    const double y = rect.y + kHeadingHeight + 12.0;
    return BLRect(rect.x, y, std::max(32.0, rect.w - controlsW), 12.0);
  }
  const double bottomControls = options.showValueEntry ? 36.0 : 0.0;
  const double x = rect.x + (rect.w - 14.0) * 0.5;
  return BLRect(x, rect.y + kHeadingHeight + 8.0, 14.0, std::max(36.0, rect.h - kHeadingHeight - bottomControls - 12.0));
}

void drawStepButton(BLContext& ctx,
                    const BLRect& rect,
                    bool increment,
                    UI_SliderOrientation orientation,
                    const UI_ButtonStyle& style,
                    bool hot,
                    bool pressed) {
  if (rect.w <= 0.0 || rect.h <= 0.0) return;
  if (hot) {
    ctx.set_fill_style(BLRgba32((style.hoverColour & 0x00FFFFFFu) | 0x66000000u));
    ctx.fill_round_rect(BLRoundRect(rect.x, rect.y, rect.w, rect.h, std::min(5.0, rect.h * 0.35)));
  }

  const double cx = rect.x + rect.w * 0.5;
  const double cy = rect.y + rect.h * 0.5;
  const double halfW = std::max(4.0, rect.w * 0.24);
  const double halfH = std::max(4.0, rect.h * 0.28);
  BLPath arrow;
  if (orientation == UI_SliderOrientation::Horizontal) {
    if (increment) {
      arrow.move_to(cx + halfW, cy);
      arrow.line_to(cx - halfW, cy - halfH);
      arrow.line_to(cx - halfW, cy + halfH);
    } else {
      arrow.move_to(cx - halfW, cy);
      arrow.line_to(cx + halfW, cy - halfH);
      arrow.line_to(cx + halfW, cy + halfH);
    }
  } else {
    if (increment) {
      arrow.move_to(cx, cy - halfH);
      arrow.line_to(cx + halfW, cy + halfH);
      arrow.line_to(cx - halfW, cy + halfH);
    } else {
      arrow.move_to(cx, cy + halfH);
      arrow.line_to(cx + halfW, cy - halfH);
      arrow.line_to(cx - halfW, cy - halfH);
    }
  }
  arrow.close();
  ctx.set_fill_style(BLRgba32(pressed ? 0xFF38BDF8u : style.textColour));
  ctx.fill_path(arrow);
}

}  // namespace

UI_SliderOptions::UI_SliderOptions(std::string_view optionsText) {
  for (const std::string& rawPart : splitTopLevel(std::string(optionsText))) {
    const size_t colon = rawPart.find(':');
    if (colon == std::string::npos) continue;
    const std::string key = lower(trim(rawPart.substr(0, colon)));
    const std::string value = trim(rawPart.substr(colon + 1));

    if (key == "orientation") {
      const std::string text = lower(unquote(value));
      orientation = (text == "vertical" || text == "v") ? UI_SliderOrientation::Vertical : UI_SliderOrientation::Horizontal;
    } else if (key == "thumb" || key == "thumbshape") {
      const std::string text = lower(unquote(value));
      if (text == "diamond") thumbShape = UI_SliderThumbShape::Diamond;
      else if (text == "triangleup" || text == "up" || text == "arrowup") thumbShape = UI_SliderThumbShape::TriangleUp;
      else if (text == "triangledown" || text == "down" || text == "arrowdown") thumbShape = UI_SliderThumbShape::TriangleDown;
      else thumbShape = UI_SliderThumbShape::Circle;
    } else if (key == "heading" || key == "title") {
      heading = unquote(value);
    } else if (key == "min" || key == "minvalue") {
      minValue = parseDouble(value, minValue);
    } else if (key == "max" || key == "maxvalue") {
      maxValue = parseDouble(value, maxValue);
    } else if (key == "default" || key == "defaultvalue" || key == "value") {
      defaultValue = parseDouble(value, defaultValue);
    } else if (key == "step") {
      step = parseDouble(value, step);
    } else if (key == "integer" || key == "int") {
      integer = parseBool(value, integer);
    } else if (key == "showvalueentry" || key == "valueentry" || key == "entry") {
      showValueEntry = parseBool(value, showValueEntry);
    } else if (key == "showstepbuttons" || key == "stepbuttons" || key == "arrows") {
      showStepButtons = parseBool(value, showStepButtons);
    } else if (key == "litleftcap") artwork.litLeftCap = unquote(value);
    else if (key == "litmiddle" || key == "litmid") artwork.litMiddle = unquote(value);
    else if (key == "litrightcap") artwork.litRightCap = unquote(value);
    else if (key == "unlitleftcap") artwork.unlitLeftCap = unquote(value);
    else if (key == "unlitmiddle" || key == "unlitmid") artwork.unlitMiddle = unquote(value);
    else if (key == "unlitrightcap") artwork.unlitRightCap = unquote(value);
    else if (key == "thumbimage" || key == "thumbartwork" || key == "thumbsvg" || key == "thumbpng") artwork.thumb = unquote(value);
  }
}

Slider::Slider(std::string id,
               BLRect rect,
               const UI_SliderOptions& options,
               const UI_ButtonStyleDefinition& style,
               double& value,
               UI_SliderState& state)
    : id_(std::move(id)), rect_(rect), options_(options), style_(&style), value_(&value), state_(&state) {}

bool SceneRenderer::UI_Slider(const std::string& id,
                                   const BLRect& rect,
                                   const UI_SliderOptions& options,
                                   double& value,
                                   const UI_ButtonStyleDefinition& style) {
  if (!frameActive_) return false;
  if (pointerCapturedByModal(id)) return false;
  auto& state = sliderStates_[id];
  Slider slider(id, rect, options, style, value, state);
  return slider.render(context_,
                       mouseX_,
                       mouseY_,
                       mouseDown_,
                       mousePressed_,
                       mouseReleased_,
                       frameSeconds_,
                       textInputEvents_,
                       keyEvents_,
                       activeSliderId_,
                       focusedSliderId_,
                       buttonResources_);
}

bool Slider::render(BLContext& ctx,
                    double mouseX,
                    double mouseY,
                    bool mouseDown,
                    bool mousePressed,
                    bool mouseReleased,
                    double seconds,
                    const std::vector<std::string>& textInputEvents,
                    const std::vector<UI_TextInputKeyEvent>& keyEvents,
                    std::string& activeSliderId,
                    std::string& focusedSliderId,
                    UI_ButtonResources& resources) const {
  UI_SliderState& state = *state_;
  double& value = *value_;
  const UI_ButtonStyle& style = style_->style();
  bool changed = false;

  if (!state.initialized) {
    value = snapValue(options_.defaultValue, options_);
    state.editText = formatValue(value, options_);
    state.initialized = true;
  } else {
    const double snapped = snapValue(value, options_);
    if (std::abs(snapped - value) > 0.0000001) {
      value = snapped;
      changed = true;
    }
  }

  const BLRect track = trackRect(rect_, options_);
  const BLRect valueRect = valueEntryRect(rect_, options_);
  const BLRect incRect = stepButtonRect(rect_, options_, true);
  const BLRect decRect = stepButtonRect(rect_, options_, false);
  const double initialRatio = valueRatio(value, options_);
  const BLRect initialThumbRect =
      options_.orientation == UI_SliderOrientation::Horizontal
          ? BLRect(track.x + track.w * initialRatio - kThumbSize * 0.5, track.y + track.h * 0.5 - kThumbSize * 0.5, kThumbSize, kThumbSize)
          : BLRect(track.x + track.w * 0.5 - kThumbSize * 0.5, track.y + track.h * (1.0 - initialRatio) - kThumbSize * 0.5, kThumbSize, kThumbSize);
  const bool overTrack = contains(track, mouseX, mouseY);
  const bool overThumb = contains(initialThumbRect, mouseX, mouseY);
  const bool overValue = contains(valueRect, mouseX, mouseY);
  const bool overInc = contains(incRect, mouseX, mouseY);
  const bool overDec = contains(decRect, mouseX, mouseY);

  auto commitEdit = [&]() {
    double parsed = value;
    if (parseValue(state.editText, parsed)) {
      const double next = snapValue(parsed, options_);
      if (std::abs(next - value) > 0.0000001) {
        value = next;
        changed = true;
      }
    }
    state.editText = formatValue(value, options_);
  };

  if (mousePressed && overValue && options_.showValueEntry) {
    focusedSliderId = id_;
    state.editingValue = true;
    state.editText = formatValue(value, options_);
  } else if (mousePressed && focusedSliderId == id_ && !overValue) {
    commitEdit();
    focusedSliderId.clear();
    state.editingValue = false;
  }

  if (focusedSliderId == id_ && state.editingValue) {
    for (const std::string& text : textInputEvents) {
      for (const char ch : text) {
        if (acceptsNumericChar(ch, state.editText)) state.editText.push_back(ch);
      }
    }
    for (const UI_TextInputKeyEvent& event : keyEvents) {
      if (event.key == SDLK_BACKSPACE && !state.editText.empty()) {
        state.editText.pop_back();
      } else if (event.key == SDLK_DELETE) {
        state.editText.clear();
      } else if (event.key == SDLK_RETURN || event.key == SDLK_KP_ENTER) {
        commitEdit();
        focusedSliderId.clear();
        state.editingValue = false;
      } else if (event.key == SDLK_ESCAPE) {
        state.editText = formatValue(value, options_);
        focusedSliderId.clear();
        state.editingValue = false;
      }
    }
  } else {
    state.editText = formatValue(value, options_);
  }

  if (mousePressed && (overTrack || overThumb) && !overValue && !overInc && !overDec) {
    activeSliderId = id_;
    state.dragging = true;
  }
  if (mouseReleased && activeSliderId == id_) {
    activeSliderId.clear();
    state.dragging = false;
  }
  if (mouseReleased) {
    state.repeatingStep = 0;
    state.nextRepeatSeconds = 0.0;
  }

  auto valueFromPointer = [&]() {
    const double t = options_.orientation == UI_SliderOrientation::Horizontal
                         ? (mouseX - track.x) / track.w
                         : 1.0 - (mouseY - track.y) / track.h;
    return options_.minValue + clampValue(t, 0.0, 1.0) * (options_.maxValue - options_.minValue);
  };

  if (state.dragging && activeSliderId == id_ && mouseDown) {
    const double next = snapValue(valueFromPointer(), options_);
    if (std::abs(next - value) > 0.0000001) {
      value = next;
      state.editText = formatValue(value, options_);
      changed = true;
    }
  }

  const double step = std::abs(options_.step) > 0.0 ? std::abs(options_.step) : 1.0;
  auto applyStep = [&](int direction) {
    const double next = snapValue(value + step * static_cast<double>(direction), options_);
    if (std::abs(next - value) > 0.0000001) {
      value = next;
      changed = true;
    }
    state.editText = formatValue(value, options_);
  };

  if (mousePressed && overInc) {
    applyStep(1);
    state.repeatingStep = 1;
    state.nextRepeatSeconds = seconds + 0.45;
  }
  if (mousePressed && overDec) {
    applyStep(-1);
    state.repeatingStep = -1;
    state.nextRepeatSeconds = seconds + 0.45;
  }
  if (mouseDown && state.repeatingStep != 0 && ((state.repeatingStep > 0 && overInc) || (state.repeatingStep < 0 && overDec))) {
    while (seconds >= state.nextRepeatSeconds && state.nextRepeatSeconds > 0.0) {
      applyStep(state.repeatingStep);
      state.nextRepeatSeconds += 0.07;
    }
  } else if (!mouseDown) {
    state.repeatingStep = 0;
    state.nextRepeatSeconds = 0.0;
  }

  if (!options_.heading.empty()) {
    drawText(ctx, BLRect(rect_.x, rect_.y, rect_.w, kHeadingHeight), style, options_.heading, resources, style.textColour);
  }

  const double ratio = valueRatio(value, options_);
  drawArtworkTrack(ctx, resources, track, options_, ratio);

  BLRect thumbRect;
  if (options_.orientation == UI_SliderOrientation::Horizontal) {
    const double x = track.x + track.w * ratio;
    thumbRect = BLRect(x - kThumbSize * 0.5, track.y + track.h * 0.5 - kThumbSize * 0.5, kThumbSize, kThumbSize);
  } else {
    const double y = track.y + track.h * (1.0 - ratio);
    thumbRect = BLRect(track.x + track.w * 0.5 - kThumbSize * 0.5, y - kThumbSize * 0.5, kThumbSize, kThumbSize);
  }
  drawThumb(ctx, resources, thumbRect, options_);

  if (options_.showValueEntry) {
    ctx.set_fill_style(BLRgba32(focusedSliderId == id_ ? style.hoverColour : style.fillColour));
    ctx.fill_round_rect(BLRoundRect(valueRect.x, valueRect.y, valueRect.w, valueRect.h, std::min(6.0, valueRect.h * 0.35)));
    ctx.set_stroke_style(BLRgba32(focusedSliderId == id_ ? style.pressedColour : style.strokeColour));
    ctx.set_stroke_width(1.0);
    ctx.stroke_round_rect(BLRoundRect(valueRect.x + 0.5, valueRect.y + 0.5, valueRect.w - 1.0, valueRect.h - 1.0, std::min(6.0, valueRect.h * 0.35)));
    drawText(ctx, BLRect(valueRect.x + 6.0, valueRect.y, valueRect.w - 12.0, valueRect.h), style, state.editText, resources, style.textColour, true);
  }

  drawStepButton(ctx, incRect, true, options_.orientation, style, overInc, mouseDown && overInc);
  drawStepButton(ctx, decRect, false, options_.orientation, style, overDec, mouseDown && overDec);
  return changed;
}

}  // namespace Blend2DUI
