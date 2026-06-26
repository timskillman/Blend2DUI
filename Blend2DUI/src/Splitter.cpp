#include "Splitter.h"

#include "SceneRenderer.h"

#include <algorithm>

namespace Blend2DUI {
namespace {

bool containsPoint(const BLRect& rect, double x, double y) {
  return x >= rect.x && y >= rect.y && x <= rect.x + rect.w && y <= rect.y + rect.h;
}

}  // namespace

UI_Splitter::UI_Splitter(UI_SplitterOrientation orientation, double ratio)
    : orientation_(orientation),
      ratio_(std::clamp(ratio, 0.0, 1.0)) {}

void UI_Splitter::setRatio(double ratio) {
  ratio_ = std::clamp(ratio, 0.0, 1.0);
}

void UI_Splitter::layout(SceneRenderer& renderer,
                         const BLRect& bounds,
                         BLRect& leadingRect,
                         BLRect& trailingRect,
                         const UI_SplitterOptions& options) {
  const bool vertical = orientation_ == UI_SplitterOrientation::Vertical;
  const double totalSize = vertical ? bounds.w : bounds.h;
  const double crossSize = vertical ? bounds.h : bounds.w;
  const double dividerGap = std::clamp(options.gap, 0.0, std::max(0.0, totalSize));
  const double availableSize = std::max(0.0, totalSize - dividerGap);
  double leadingMin = std::max(0.0, options.leadingMinSize);
  double trailingMin = std::max(0.0, options.trailingMinSize);
  if (availableSize <= 0.0) {
    leadingMin = 0.0;
    trailingMin = 0.0;
  } else if (leadingMin + trailingMin > availableSize) {
    const double scale = availableSize / (leadingMin + trailingMin);
    leadingMin *= scale;
    trailingMin *= scale;
  }
  const double minRatio = availableSize > 0.0 ? std::min(0.95, leadingMin / availableSize) : 0.5;
  const double maxRatio = availableSize > 0.0 ? std::max(minRatio, 1.0 - (trailingMin / availableSize)) : minRatio;
  ratio_ = std::clamp(ratio_, minRatio, maxRatio);

  const double maxLeadingSize = std::max(leadingMin, availableSize - trailingMin);
  double leadingSize = std::clamp(availableSize * ratio_, leadingMin, maxLeadingSize);
  double dividerAxis = vertical ? bounds.x + leadingSize : bounds.y + leadingSize;

  dividerRect_ = vertical
                     ? BLRect(dividerAxis, bounds.y, dividerGap, crossSize)
                     : BLRect(bounds.x, dividerAxis, crossSize, dividerGap);
  hitRect_ = vertical
                 ? BLRect(dividerRect_.x - options.hitPadding,
                          dividerRect_.y,
                          dividerRect_.w + options.hitPadding * 2.0,
                          dividerRect_.h)
                 : BLRect(dividerRect_.x,
                          dividerRect_.y - options.hitPadding,
                          dividerRect_.w,
                          dividerRect_.h + options.hitPadding * 2.0);

  hovered_ = containsPoint(hitRect_, renderer.mouseX(), renderer.mouseY());
  if (renderer.mousePressed() && hovered_) {
    dragging_ = true;
    grabOffset_ = vertical ? (renderer.mouseX() - dividerRect_.x) : (renderer.mouseY() - dividerRect_.y);
  }

  if (dragging_ && renderer.mouseDown()) {
    const double mouseAxis = vertical ? renderer.mouseX() : renderer.mouseY();
    const double minAxis = vertical ? (bounds.x + leadingMin) : (bounds.y + leadingMin);
    const double maxAxis = vertical ? (bounds.x + totalSize - dividerGap - trailingMin)
                                    : (bounds.y + totalSize - dividerGap - trailingMin);
    dividerAxis = std::clamp(mouseAxis - grabOffset_, minAxis, maxAxis);
    leadingSize = dividerAxis - (vertical ? bounds.x : bounds.y);
    ratio_ = availableSize > 0.0 ? (leadingSize / availableSize) : ratio_;
    dividerRect_ = vertical
                       ? BLRect(dividerAxis, bounds.y, dividerGap, crossSize)
                       : BLRect(bounds.x, dividerAxis, crossSize, dividerGap);
    hitRect_ = vertical
                   ? BLRect(dividerRect_.x - options.hitPadding,
                            dividerRect_.y,
                            dividerRect_.w + options.hitPadding * 2.0,
                            dividerRect_.h)
                   : BLRect(dividerRect_.x,
                            dividerRect_.y - options.hitPadding,
                            dividerRect_.w,
                            dividerRect_.h + options.hitPadding * 2.0);
  }

  if (dragging_ && renderer.mouseReleased()) {
    dragging_ = false;
  }

  hovered_ = containsPoint(hitRect_, renderer.mouseX(), renderer.mouseY());
  const double trailingSize = std::max(trailingMin, totalSize - leadingSize - dividerGap);
  leadingRect = vertical ? BLRect(bounds.x, bounds.y, leadingSize, crossSize)
                         : BLRect(bounds.x, bounds.y, crossSize, leadingSize);
  trailingRect = vertical
                     ? BLRect(leadingRect.x + leadingRect.w + dividerGap, bounds.y, trailingSize, crossSize)
                     : BLRect(bounds.x, leadingRect.y + leadingRect.h + dividerGap, crossSize, trailingSize);
}

void UI_Splitter::render(SceneRenderer& renderer, const UI_SplitterOptions& options) const {
  if (dividerRect_.w <= 0.0 || dividerRect_.h <= 0.0) return;

  BLContext& canvas = renderer.context();
  const bool vertical = orientation_ == UI_SplitterOrientation::Vertical;
  const uint32_t dividerColour = dragging_ ? options.activeColour : hovered_ ? options.hoverColour : options.idleColour;
  const double axisSize = vertical ? dividerRect_.h : dividerRect_.w;
  double gripOffset = std::min(options.gripInset, axisSize * 0.5);
  double gripLength = std::max(0.0, axisSize - gripOffset * 2.0);
  if (axisSize > options.minGripLength && gripLength < options.minGripLength) {
    gripOffset = (axisSize - options.minGripLength) * 0.5;
    gripLength = options.minGripLength;
  }
  const BLRect gripRect = vertical
                              ? BLRect(dividerRect_.x + (dividerRect_.w - options.gripThickness) * 0.5,
                                       dividerRect_.y + gripOffset,
                                       options.gripThickness,
                                       gripLength)
                              : BLRect(dividerRect_.x + gripOffset,
                                       dividerRect_.y + (dividerRect_.h - options.gripThickness) * 0.5,
                                       gripLength,
                                       options.gripThickness);

  canvas.set_fill_style(BLRgba32(dividerColour));
  canvas.fill_round_rect(BLRoundRect(gripRect.x,
                                     gripRect.y,
                                     gripRect.w,
                                     gripRect.h,
                                     std::min(gripRect.w, gripRect.h) * 0.5));

  canvas.set_fill_style(BLRgba32(options.gripColour));
  for (int i = 0; i < 3; ++i) {
    const double offset = (static_cast<double>(i) - 1.0) * 9.0;
    const double cx = vertical ? dividerRect_.x + dividerRect_.w * 0.5 : dividerRect_.x + dividerRect_.w * 0.5 + offset;
    const double cy = vertical ? dividerRect_.y + dividerRect_.h * 0.5 + offset : dividerRect_.y + dividerRect_.h * 0.5;
    canvas.fill_circle(BLCircle(cx, cy, 1.6));
  }
}

}  // namespace Blend2DUI
