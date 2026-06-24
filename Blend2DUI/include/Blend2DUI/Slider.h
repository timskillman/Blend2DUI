#pragma once

#include "Blend2DUI/Button.h"
#include "Blend2DUI/TextInput.h"

#include <SDL3/SDL.h>
#include <blend2d/blend2d.h>

#include <string>
#include <vector>

namespace Blend2DUI {

enum class UI_SliderOrientation {
  Horizontal,
  Vertical
};

enum class UI_SliderThumbShape {
  Circle,
  Diamond,
  TriangleUp,
  TriangleDown
};

struct UI_SliderArtwork {
  std::string litLeftCap;
  std::string litMiddle;
  std::string litRightCap;
  std::string unlitLeftCap;
  std::string unlitMiddle;
  std::string unlitRightCap;
  std::string thumb;
};

struct UI_SliderOptions {
  UI_SliderOrientation orientation = UI_SliderOrientation::Horizontal;
  UI_SliderThumbShape thumbShape = UI_SliderThumbShape::Circle;
  UI_SliderArtwork artwork;
  std::string heading;
  double minValue = 0.0;
  double maxValue = 100.0;
  double defaultValue = 0.0;
  double step = 1.0;
  bool integer = false;
  bool showValueEntry = true;
  bool showStepButtons = true;
};

struct UI_SliderState {
  bool initialized = false;
  bool dragging = false;
  bool editingValue = false;
  int repeatingStep = 0;
  double nextRepeatSeconds = 0.0;
  std::string editText;
};

class Slider {
 public:
  Slider(std::string id,
         BLRect rect,
         const UI_SliderOptions& options,
         const UI_ButtonStyleDefinition& style,
         double& value,
         UI_SliderState& state);

  bool render(BLContext& ctx,
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
              UI_ButtonResources& resources) const;

 private:
  std::string id_;
  BLRect rect_;
  UI_SliderOptions options_;
  const UI_ButtonStyleDefinition* style_ = nullptr;
  double* value_ = nullptr;
  UI_SliderState* state_ = nullptr;
};

}  // namespace Blend2DUI
