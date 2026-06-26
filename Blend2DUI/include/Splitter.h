#pragma once

#include <blend2d/blend2d.h>

#include <cstdint>

namespace Blend2DUI {

class SceneRenderer;

enum class UI_SplitterOrientation {
  Vertical,
  Horizontal
};

struct UI_SplitterOptions {
  double gap = 20.0;
  double leadingMinSize = 60.0;
  double trailingMinSize = 60.0;
  double hitPadding = 4.0;
  double gripThickness = 6.0;
  double gripInset = 14.0;
  double minGripLength = 24.0;
  uint32_t idleColour = 0xFFE2E8F0u;
  uint32_t hoverColour = 0xFF94A3B8u;
  uint32_t activeColour = 0xFF38BDF8u;
  uint32_t gripColour = 0xFFFFFFFFu;
};

class UI_Splitter {
 public:
  explicit UI_Splitter(UI_SplitterOrientation orientation = UI_SplitterOrientation::Vertical,
                       double ratio = 0.5);

  void setRatio(double ratio);
  double ratio() const { return ratio_; }
  bool dragging() const { return dragging_; }

  void layout(SceneRenderer& renderer,
              const BLRect& bounds,
              BLRect& leadingRect,
              BLRect& trailingRect,
              const UI_SplitterOptions& options = UI_SplitterOptions{});
  void render(SceneRenderer& renderer, const UI_SplitterOptions& options = UI_SplitterOptions{}) const;

 private:
  UI_SplitterOrientation orientation_ = UI_SplitterOrientation::Vertical;
  double ratio_ = 0.5;
  double grabOffset_ = 0.0;
  bool dragging_ = false;
  bool hovered_ = false;
  BLRect dividerRect_{};
  BLRect hitRect_{};
};

}  // namespace Blend2DUI
