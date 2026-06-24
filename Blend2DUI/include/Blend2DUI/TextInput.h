#pragma once

#include "Blend2DUI/Button.h"

#include <SDL3/SDL.h>
#include <blend2d/blend2d.h>

#include <string>
#include <string_view>
#include <vector>

namespace Blend2DUI {

enum class UI_TextInputMode {
  SingleLine,
  MultiLine
};

enum class UI_TextInputFilter {
  All,
  Numbers,
  Calculator,
  Password
};

struct UI_TextInputOptions {
  UI_TextInputOptions() = default;
  explicit UI_TextInputOptions(std::string_view optionsText);

  UI_TextInputMode mode = UI_TextInputMode::SingleLine;
  UI_TextInputFilter filter = UI_TextInputFilter::All;
  bool resizable = false;
  bool passwordVisible = false;
  size_t maxLength = 256;
  std::string placeholder;
};

struct UI_TextInputKeyEvent {
  SDL_Keycode key = 0;
  SDL_Keymod mod = SDL_KMOD_NONE;
  bool repeat = false;
};

struct UI_TextInputHistorySnapshot {
  std::string text;
  size_t caret = 0;
  size_t selectionAnchor = 0;
};

struct UI_TextInputLayoutLine {
  size_t begin = 0;
  size_t end = 0;
  double width = 0.0;
  std::vector<size_t> offsets;
  std::vector<double> xPositions;
};

struct UI_TextInputState {
  BLRect rect;
  size_t caret = 0;
  size_t selectionAnchor = 0;
  size_t scrollLine = 0;
  double scrollX = 0.0;
  double preferredCaretX = -1.0;
  double scrollbarDragOffsetY = 0.0;
  std::vector<UI_TextInputHistorySnapshot> undoStack;
  std::vector<UI_TextInputHistorySnapshot> redoStack;
  std::string cachedLayoutText;
  double cachedLayoutWidth = -1.0;
  double cachedLayoutFontSize = -1.0;
  bool cachedLayoutMultiLine = false;
  std::vector<UI_TextInputLayoutLine> cachedLayoutLines;
  bool draggingSelection = false;
  bool draggingScrollbar = false;
  bool resizing = false;
};

class TextInput {
 public:
  TextInput(std::string id,
            BLRect rect,
            const UI_TextInputOptions& options,
            const UI_ButtonStyleDefinition& style,
            std::string& text,
            UI_TextInputState& state);

  bool render(BLContext& ctx,
              double mouseX,
              double mouseY,
              bool mouseDown,
              bool mousePressed,
              bool mouseReleased,
              double wheelY,
              double seconds,
              std::string& focusedTextInputId,
              std::string& activeTextInputId,
              const std::vector<std::string>& textInputEvents,
              const std::vector<UI_TextInputKeyEvent>& keyEvents,
              UI_ButtonResources& resources) const;

 private:
  std::string id_;
  BLRect rect_;
  UI_TextInputOptions options_;
  const UI_ButtonStyleDefinition* style_ = nullptr;
  std::string* text_ = nullptr;
  UI_TextInputState* state_ = nullptr;
};

}  // namespace Blend2DUI
