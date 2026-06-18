#include "Blend2DUI/TextInput.h"
#include "Blend2DUI/FontManager.h"
#include "Blend2DUI/SdlBlend2DRenderer.h"

#include <algorithm>
#include <cmath>

namespace Blend2DUI {
namespace {

constexpr double kPaddingX = 10.0;
constexpr double kPaddingY = 6.0;
constexpr double kHandleSize = 14.0;
constexpr double kScrollbarWidth = 10.0;
constexpr double kScrollbarHitWidth = 18.0;
constexpr size_t kMaxHistorySnapshots = 100;

bool contains(const BLRect& rect, double x, double y) {
  return x >= rect.x && y >= rect.y && x < rect.x + rect.w && y < rect.y + rect.h;
}

BLRect insetRect(const BLRect& rect, double inset) {
  return BLRect(rect.x + inset, rect.y + inset, std::max(0.0, rect.w - inset * 2.0), std::max(0.0, rect.h - inset * 2.0));
}

BLRect insetRect(const BLRect& rect, double insetX, double insetY) {
  return BLRect(rect.x + insetX,
                rect.y + insetY,
                std::max(0.0, rect.w - insetX * 2.0),
                std::max(0.0, rect.h - insetY * 2.0));
}

double clampCorner(double corner, const BLRect& rect) {
  return std::max(0.0, std::min(corner, std::min(rect.w, rect.h) * 0.5));
}

bool isContinuation(unsigned char ch) {
  return (ch & 0xC0u) == 0x80u;
}

std::vector<size_t> utf8Offsets(const std::string& text) {
  std::vector<size_t> offsets;
  offsets.push_back(0);
  for (size_t i = 0; i < text.size();) {
    const unsigned char ch = static_cast<unsigned char>(text[i]);
    size_t advance = 1;
    if ((ch & 0xE0u) == 0xC0u) advance = 2;
    else if ((ch & 0xF0u) == 0xE0u) advance = 3;
    else if ((ch & 0xF8u) == 0xF0u) advance = 4;
    i = std::min(text.size(), i + advance);
    while (i < text.size() && isContinuation(static_cast<unsigned char>(text[i]))) ++i;
    offsets.push_back(i);
  }
  return offsets;
}

size_t prevUtf8(const std::string& text, size_t offset) {
  if (offset == 0) return 0;
  --offset;
  while (offset > 0 && isContinuation(static_cast<unsigned char>(text[offset]))) --offset;
  return offset;
}

size_t nextUtf8(const std::string& text, size_t offset) {
  if (offset >= text.size()) return text.size();
  ++offset;
  while (offset < text.size() && isContinuation(static_cast<unsigned char>(text[offset]))) ++offset;
  return offset;
}

size_t codepointCount(const std::string& text) {
  const auto offsets = utf8Offsets(text);
  return offsets.empty() ? 0 : offsets.size() - 1;
}

size_t clampToUtf8Boundary(const std::string& text, size_t offset) {
  offset = std::min(offset, text.size());
  while (offset > 0 && isContinuation(static_cast<unsigned char>(text[offset]))) --offset;
  return offset;
}

std::string filteredText(const std::string& input, UI_TextInputFilter filter, bool multiLine) {
  if (filter == UI_TextInputFilter::All || filter == UI_TextInputFilter::Password) {
    if (multiLine) return input;
    std::string out;
    for (char ch : input) {
      if (ch != '\r' && ch != '\n') out.push_back(ch);
    }
    return out;
  }

  const std::string allowed = filter == UI_TextInputFilter::Numbers ? "0123456789.-" : "0123456789.+-*/()% ";
  std::string out;
  for (unsigned char ch : input) {
    if (allowed.find(static_cast<char>(ch)) != std::string::npos) out.push_back(static_cast<char>(ch));
  }
  return out;
}

double measureText(BLFont& font, std::string_view text) {
  if (text.empty() || !font.is_valid()) return 0.0;
  BLGlyphBuffer glyphs;
  BLTextMetrics metrics;
  glyphs.set_utf8_text(text.data(), text.size());
  font.shape(glyphs);
  font.get_text_metrics(glyphs, metrics);
  return metrics.advance.x;
}

std::vector<UI_TextInputLayoutLine> buildLayoutLines(const std::string& text, BLFont& font, double maxWidth, bool multiLine) {
  std::vector<UI_TextInputLayoutLine> lines;
  const auto offsets = utf8Offsets(text);

  auto appendLine = [&](size_t begin, size_t end) {
    UI_TextInputLayoutLine line;
    line.begin = begin;
    line.end = end;
    line.offsets.push_back(begin);
    line.xPositions.push_back(0.0);
    double x = 0.0;
    auto first = std::lower_bound(offsets.begin(), offsets.end(), begin);
    auto last = std::lower_bound(offsets.begin(), offsets.end(), end);
    for (auto it = first; it != last && std::next(it) != offsets.end(); ++it) {
      const size_t cpBegin = *it;
      const size_t cpEnd = *std::next(it);
      if (cpBegin >= end) break;
      x += measureText(font, std::string_view(text).substr(cpBegin, std::min(cpEnd, end) - cpBegin));
      line.offsets.push_back(std::min(cpEnd, end));
      line.xPositions.push_back(x);
    }
    line.width = x;
    lines.push_back(std::move(line));
  };

  if (!multiLine) {
    appendLine(0, text.size());
    return lines;
  }

  size_t lineBegin = 0;
  size_t lastBreak = 0;
  size_t lastBreakNext = 0;
  size_t lastBreakIndex = 0;
  double lastBreakWidth = 0.0;
  double lineWidth = 0.0;
  for (size_t i = 0; i + 1 < offsets.size(); ++i) {
    const size_t begin = offsets[i];
    const size_t end = offsets[i + 1];
    const char ch = text[begin];
    if (ch == '\r' || ch == '\n') {
      appendLine(lineBegin, begin);
      lineBegin = end;
      lineWidth = 0.0;
      i = std::distance(offsets.begin(), std::lower_bound(offsets.begin(), offsets.end(), lineBegin));
      if (i > 0) --i;
      lastBreak = lastBreakNext = 0;
      lastBreakIndex = 0;
      lastBreakWidth = 0.0;
      continue;
    }
    const double charWidth = measureText(font, std::string_view(text).substr(begin, end - begin));
    const double nextWidth = lineWidth + charWidth;
    if ((ch == ' ' || ch == '\t') && begin > lineBegin) {
      lastBreak = begin;
      lastBreakNext = end;
      lastBreakIndex = i;
      lastBreakWidth = lineWidth;
    }
    if (maxWidth > 12.0 && nextWidth > maxWidth && begin > lineBegin) {
      const bool wrapAtBreak = lastBreak > lineBegin;
      const size_t wrapEnd = wrapAtBreak ? lastBreak : begin;
      appendLine(lineBegin, wrapEnd);
      lineBegin = wrapAtBreak ? lastBreakNext : begin;
      i = std::distance(offsets.begin(), std::lower_bound(offsets.begin(), offsets.end(), lineBegin));
      if (i > 0) --i;
      lineWidth = 0.0;
      lastBreak = lastBreakNext = 0;
      lastBreakIndex = 0;
      lastBreakWidth = 0.0;
    } else {
      lineWidth = nextWidth;
    }
  }
  appendLine(lineBegin, text.size());
  return lines;
}

const std::vector<UI_TextInputLayoutLine>& layoutLines(std::string& text,
                                                       BLFont& font,
                                                       double maxWidth,
                                                       bool multiLine,
                                                       double fontSize,
                                                       UI_TextInputState& state) {
  if (state.cachedLayoutText != text ||
      state.cachedLayoutWidth != maxWidth ||
      state.cachedLayoutFontSize != fontSize ||
      state.cachedLayoutMultiLine != multiLine) {
    state.cachedLayoutText = text;
    state.cachedLayoutWidth = maxWidth;
    state.cachedLayoutFontSize = fontSize;
    state.cachedLayoutMultiLine = multiLine;
    state.cachedLayoutLines = buildLayoutLines(text, font, maxWidth, multiLine);
  }
  return state.cachedLayoutLines;
}

size_t lineForCaret(const std::vector<UI_TextInputLayoutLine>& lines, size_t caret) {
  for (size_t i = 0; i < lines.size(); ++i) {
    if (caret >= lines[i].begin && caret <= lines[i].end) return i;
  }
  return lines.empty() ? 0 : lines.size() - 1;
}

double caretX(const UI_TextInputLayoutLine& line, size_t caret) {
  caret = std::min(caret, line.end);
  auto it = std::lower_bound(line.offsets.begin(), line.offsets.end(), caret);
  if (it != line.offsets.end() && *it == caret) {
    return line.xPositions[static_cast<size_t>(std::distance(line.offsets.begin(), it))];
  }
  if (it == line.offsets.begin()) return 0.0;
  return line.xPositions[static_cast<size_t>(std::distance(line.offsets.begin(), std::prev(it)))];
}

size_t hitTextOffset(const UI_TextInputLayoutLine& line, double x) {
  size_t best = line.begin;
  double bestDistance = std::abs(x);
  for (size_t i = 0; i < line.offsets.size(); ++i) {
    const size_t offset = line.offsets[i];
    const double px = line.xPositions[i];
    const double distance = std::abs(px - x);
    if (distance < bestDistance) {
      bestDistance = distance;
      best = offset;
    }
  }
  return best;
}

struct VisibleTextRun {
  size_t begin = 0;
  size_t end = 0;
  double x = 0.0;
};

VisibleTextRun visibleTextRunForClip(const std::string& text,
                                     const UI_TextInputLayoutLine& line,
                                     double lineX,
                                     double clipX,
                                     double clipW) {
  if (line.end <= line.begin || clipW <= 0.0) return {};

  const double visibleLeft = clipX - lineX;
  const double visibleRight = clipX + clipW - lineX;
  if (visibleRight <= 0.0 || visibleLeft >= line.width) return {};

  size_t begin = line.begin;
  size_t end = line.end;
  double beginX = 0.0;

  for (size_t i = 0; i + 1 < line.offsets.size(); ++i) {
    const size_t cpBegin = line.offsets[i];
    const double cpEndX = line.xPositions[i + 1];
    if (cpEndX >= visibleLeft) {
      begin = cpBegin;
      beginX = line.xPositions[i];
      break;
    }
  }

  for (size_t i = 0; i < line.offsets.size(); ++i) {
    const size_t cpBegin = line.offsets[i];
    if (line.xPositions[i] > visibleRight) {
      end = cpBegin;
      break;
    }
  }

  if (end <= begin) return {};
  return VisibleTextRun{begin, end, lineX + beginX};
}

void eraseSelection(std::string& text, UI_TextInputState& state) {
  const size_t a = std::min(state.caret, state.selectionAnchor);
  const size_t b = std::max(state.caret, state.selectionAnchor);
  if (a == b) return;
  text.erase(a, b - a);
  state.caret = state.selectionAnchor = a;
}

bool hasSelection(const UI_TextInputState& state) {
  return state.caret != state.selectionAnchor;
}

bool isAutoRepeatNavigationKey(SDL_Keycode key) {
  return key == SDLK_LEFT || key == SDLK_RIGHT || key == SDLK_UP || key == SDLK_DOWN;
}

bool isAutoRepeatEditingKey(SDL_Keycode key) {
  return key == SDLK_BACKSPACE || key == SDLK_DELETE;
}

UI_TextInputHistorySnapshot makeHistorySnapshot(const std::string& text, const UI_TextInputState& state) {
  return UI_TextInputHistorySnapshot{text, state.caret, state.selectionAnchor};
}

bool sameHistorySnapshot(const UI_TextInputHistorySnapshot& snapshot, const std::string& text, const UI_TextInputState& state) {
  return snapshot.text == text && snapshot.caret == state.caret && snapshot.selectionAnchor == state.selectionAnchor;
}

void trimHistory(std::vector<UI_TextInputHistorySnapshot>& stack) {
  if (stack.size() > kMaxHistorySnapshots) {
    stack.erase(stack.begin(), stack.begin() + static_cast<std::ptrdiff_t>(stack.size() - kMaxHistorySnapshots));
  }
}

void pushUndoSnapshot(std::string& text, UI_TextInputState& state) {
  if (state.undoStack.empty() || !sameHistorySnapshot(state.undoStack.back(), text, state)) {
    state.undoStack.push_back(makeHistorySnapshot(text, state));
    trimHistory(state.undoStack);
  }
  state.redoStack.clear();
}

void restoreHistorySnapshot(std::string& text, UI_TextInputState& state, const UI_TextInputHistorySnapshot& snapshot) {
  text = snapshot.text;
  state.caret = clampToUtf8Boundary(text, snapshot.caret);
  state.selectionAnchor = clampToUtf8Boundary(text, snapshot.selectionAnchor);
  state.preferredCaretX = -1.0;
}

bool undoTextEdit(std::string& text, UI_TextInputState& state) {
  if (state.undoStack.empty()) return false;
  state.redoStack.push_back(makeHistorySnapshot(text, state));
  trimHistory(state.redoStack);
  const UI_TextInputHistorySnapshot snapshot = state.undoStack.back();
  state.undoStack.pop_back();
  restoreHistorySnapshot(text, state, snapshot);
  return true;
}

bool redoTextEdit(std::string& text, UI_TextInputState& state) {
  if (state.redoStack.empty()) return false;
  state.undoStack.push_back(makeHistorySnapshot(text, state));
  trimHistory(state.undoStack);
  const UI_TextInputHistorySnapshot snapshot = state.redoStack.back();
  state.redoStack.pop_back();
  restoreHistorySnapshot(text, state, snapshot);
  return true;
}

}  // namespace

TextInput::TextInput(std::string id,
                     BLRect rect,
                     const UI_TextInputOptions& options,
                     const UI_ButtonStyleDefinition& style,
                     std::string& text,
                     UI_TextInputState& state)
    : id_(std::move(id)), rect_(rect), options_(options), style_(&style), text_(&text), state_(&state) {}

bool SdlBlend2DRenderer::UI_TextInput(const std::string& id,
                                      const BLRect& rect,
                                      const UI_TextInputOptions& options,
                                      std::string& text,
                                      const UI_ButtonStyleDefinition& style) {
  if (!frameActive_) return false;
  if (pointerCapturedByModal(id)) return false;
  auto& state = textInputStates_[id];
  TextInput input(id, rect, options, style, text, state);
  return input.render(context_, mouseX_, mouseY_, mouseDown_, mousePressed_, mouseReleased_, wheelY_, frameSeconds_,
                      focusedTextInputId_, activeTextInputId_, textInputEvents_, keyEvents_, buttonResources_);
}

bool TextInput::render(BLContext& ctx,
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
                       UI_ButtonResources& resources) const {
  std::string& text = *text_;
  UI_TextInputState& state = *state_;
  if (!options_.resizable || state.rect.w <= 0.0 || state.rect.h <= 0.0) state.rect = rect_;
  state.caret = clampToUtf8Boundary(text, state.caret);
  state.selectionAnchor = clampToUtf8Boundary(text, state.selectionAnchor);

  const UI_ButtonStyle& style = style_->style();
  UI_ButtonStyle renderStyle = style;
  BLFont font = FontManager::loadFont(resources, renderStyle);
  const BLFontMetrics fontMetrics = font.metrics();
  const double lineHeight = std::max(10.0, fontMetrics.ascent + fontMetrics.descent + 4.0);
  const bool multiLine = options_.mode == UI_TextInputMode::MultiLine;
  const bool focused = focusedTextInputId == id_;
  const BLRect textRect = insetRect(state.rect, kPaddingX, kPaddingY);
  const BLRect handleRect(state.rect.x + state.rect.w - kHandleSize, state.rect.y + state.rect.h - kHandleSize, kHandleSize, kHandleSize);

  const auto displayText = [&]() {
    if (options_.filter != UI_TextInputFilter::Password || options_.passwordVisible) return text;
    return std::string(codepointCount(text), '*');
  };
  std::string visibleText = displayText();
  const std::vector<UI_TextInputLayoutLine>& lines =
      layoutLines(visibleText, font, textRect.w - (multiLine ? kScrollbarWidth + 4.0 : 0.0), multiLine, renderStyle.fontSize, state);
  const size_t visibleLineCount = std::max<size_t>(1, static_cast<size_t>(std::floor(textRect.h / lineHeight)));
  const bool hasScrollbar = multiLine && lines.size() > visibleLineCount;
  const size_t maxInitialScroll = hasScrollbar ? lines.size() - visibleLineCount : 0;
  const double scrollbarTrackH = textRect.h;
  const double scrollbarThumbH = hasScrollbar
                                     ? std::max(24.0, scrollbarTrackH * static_cast<double>(visibleLineCount) / lines.size())
                                     : scrollbarTrackH;
  const double scrollbarTrackX = state.rect.x + state.rect.w - kPaddingX - kScrollbarWidth;
  const BLRect scrollbarTrackRect(scrollbarTrackX, textRect.y, kScrollbarWidth, scrollbarTrackH);
  const BLRect scrollbarHitRect(state.rect.x + state.rect.w - kScrollbarHitWidth - 4.0,
                                textRect.y,
                                kScrollbarHitWidth + 4.0,
                                scrollbarTrackH);

  auto thumbYFromScroll = [&](size_t scrollLine) {
    if (!hasScrollbar || maxInitialScroll == 0) return textRect.y;
    const double scrollT = static_cast<double>(std::min(scrollLine, maxInitialScroll)) / static_cast<double>(maxInitialScroll);
    return textRect.y + (scrollbarTrackH - scrollbarThumbH) * scrollT;
  };

  auto scrollLineFromThumbY = [&](double thumbY) {
    if (!hasScrollbar || maxInitialScroll == 0 || scrollbarTrackH <= scrollbarThumbH) return size_t(0);
    const double top = std::max(textRect.y, std::min(textRect.y + scrollbarTrackH - scrollbarThumbH, thumbY));
    const double t = (top - textRect.y) / (scrollbarTrackH - scrollbarThumbH);
    return std::min(maxInitialScroll, static_cast<size_t>(std::round(t * static_cast<double>(maxInitialScroll))));
  };

  const double scrollbarThumbY = thumbYFromScroll(state.scrollLine);
  const BLRect scrollbarThumbRect(scrollbarTrackX, scrollbarThumbY, kScrollbarWidth, scrollbarThumbH);

  auto offsetAtMouse = [&](double x, double y) {
    const size_t lineIndex = multiLine
                                 ? std::min(lines.size() - 1, state.scrollLine + static_cast<size_t>(std::max(0.0, std::floor((y - textRect.y) / lineHeight))))
                                 : size_t(0);
    const double localX = x - textRect.x + state.scrollX;
    return hitTextOffset(lines[lineIndex], localX);
  };

  bool changed = false;
  if (mousePressed) {
    if (options_.resizable && contains(handleRect, mouseX, mouseY)) {
      focusedTextInputId = id_;
      activeTextInputId = id_;
      state.resizing = true;
    } else if (hasScrollbar && contains(scrollbarHitRect, mouseX, mouseY)) {
      focusedTextInputId = id_;
      activeTextInputId = id_;
      state.draggingScrollbar = true;
      state.draggingSelection = false;
      if (contains(scrollbarThumbRect, mouseX, mouseY)) {
        state.scrollbarDragOffsetY = mouseY - scrollbarThumbY;
      } else {
        state.scrollbarDragOffsetY = scrollbarThumbH * 0.5;
        state.scrollLine = scrollLineFromThumbY(mouseY - state.scrollbarDragOffsetY);
      }
    } else if (contains(state.rect, mouseX, mouseY)) {
      focusedTextInputId = id_;
      activeTextInputId = id_;
      state.draggingSelection = true;
      state.caret = state.selectionAnchor = offsetAtMouse(mouseX, mouseY);
      state.preferredCaretX = -1.0;
    } else if (focused) {
      focusedTextInputId.clear();
    }
  }

  if (activeTextInputId == id_ && state.resizing && mouseDown) {
    state.rect.w = std::max(80.0, mouseX - state.rect.x + 4.0);
    state.rect.h = std::max(multiLine ? 80.0 : 28.0, mouseY - state.rect.y + 4.0);
  } else if (activeTextInputId == id_ && state.draggingScrollbar && mouseDown) {
    state.scrollLine = scrollLineFromThumbY(mouseY - state.scrollbarDragOffsetY);
  } else if (activeTextInputId == id_ && state.draggingSelection && mouseDown) {
    state.caret = offsetAtMouse(mouseX, mouseY);
    state.preferredCaretX = -1.0;
  }
  if (mouseReleased && activeTextInputId == id_) {
    state.draggingSelection = false;
    state.draggingScrollbar = false;
    state.resizing = false;
    activeTextInputId.clear();
  }

  if (focusedTextInputId == id_) {
    if (multiLine && wheelY != 0.0 && contains(state.rect, mouseX, mouseY)) {
      const size_t maxScroll = lines.size() > visibleLineCount ? lines.size() - visibleLineCount : 0;
      if (wheelY < 0.0) state.scrollLine = std::min(maxScroll, state.scrollLine + 3);
      else state.scrollLine = state.scrollLine > 3 ? state.scrollLine - 3 : 0;
    }

    for (const UI_TextInputKeyEvent& event : keyEvents) {
      if (event.repeat && !isAutoRepeatNavigationKey(event.key) && !isAutoRepeatEditingKey(event.key)) continue;

      const bool ctrl = (event.mod & SDL_KMOD_CTRL) != 0;
      const bool shift = (event.mod & SDL_KMOD_SHIFT) != 0;
      if (ctrl && event.key == SDLK_Z) {
        changed = (shift ? redoTextEdit(text, state) : undoTextEdit(text, state)) || changed;
        continue;
      }
      if (ctrl && event.key == SDLK_Y) {
        changed = redoTextEdit(text, state) || changed;
        continue;
      }
      if (ctrl && event.key == SDLK_A) {
        state.selectionAnchor = 0;
        state.caret = text.size();
        state.preferredCaretX = -1.0;
        continue;
      }
      if (ctrl && (event.key == SDLK_C || event.key == SDLK_X) && hasSelection(state)) {
        const size_t a = std::min(state.caret, state.selectionAnchor);
        const size_t b = std::max(state.caret, state.selectionAnchor);
        SDL_SetClipboardText(text.substr(a, b - a).c_str());
        if (event.key == SDLK_X) {
          pushUndoSnapshot(text, state);
          eraseSelection(text, state);
          changed = true;
        }
        continue;
      }
      if (ctrl && event.key == SDLK_V) {
        char* clipboard = SDL_GetClipboardText();
        if (clipboard && *clipboard) {
          std::string paste = filteredText(clipboard, options_.filter, multiLine);
          if (!multiLine && options_.maxLength > 0) {
            const size_t room = options_.maxLength > codepointCount(text) ? options_.maxLength - codepointCount(text) : 0;
            const auto offsets = utf8Offsets(paste);
            paste.resize(offsets[std::min(room, offsets.size() - 1)]);
          }
          if (!paste.empty() || hasSelection(state)) {
            pushUndoSnapshot(text, state);
            eraseSelection(text, state);
            text.insert(state.caret, paste);
            state.caret += paste.size();
            state.selectionAnchor = state.caret;
            changed = true;
          }
        }
        if (clipboard) SDL_free(clipboard);
        continue;
      }

      if (event.key == SDLK_BACKSPACE || event.key == SDLK_DELETE) {
        if (hasSelection(state)) {
          pushUndoSnapshot(text, state);
          eraseSelection(text, state);
          changed = true;
        } else if (event.key == SDLK_BACKSPACE && state.caret > 0) {
          pushUndoSnapshot(text, state);
          const size_t prev = prevUtf8(text, state.caret);
          text.erase(prev, state.caret - prev);
          state.caret = state.selectionAnchor = prev;
          changed = true;
        } else if (event.key == SDLK_DELETE && state.caret < text.size()) {
          pushUndoSnapshot(text, state);
          const size_t next = nextUtf8(text, state.caret);
          text.erase(state.caret, next - state.caret);
          state.selectionAnchor = state.caret;
          changed = true;
        }
      } else if (event.key == SDLK_LEFT) {
        state.caret = prevUtf8(text, state.caret);
        if (!(event.mod & SDL_KMOD_SHIFT)) state.selectionAnchor = state.caret;
        state.preferredCaretX = -1.0;
      } else if (event.key == SDLK_RIGHT) {
        state.caret = nextUtf8(text, state.caret);
        if (!(event.mod & SDL_KMOD_SHIFT)) state.selectionAnchor = state.caret;
        state.preferredCaretX = -1.0;
      } else if (event.key == SDLK_HOME) {
        const size_t li = lineForCaret(lines, state.caret);
        state.caret = lines[li].begin;
        if (!(event.mod & SDL_KMOD_SHIFT)) state.selectionAnchor = state.caret;
      } else if (event.key == SDLK_END) {
        const size_t li = lineForCaret(lines, state.caret);
        state.caret = lines[li].end;
        if (!(event.mod & SDL_KMOD_SHIFT)) state.selectionAnchor = state.caret;
      } else if (multiLine && (event.key == SDLK_UP || event.key == SDLK_DOWN)) {
        const size_t li = lineForCaret(lines, state.caret);
        const double currentX = state.preferredCaretX >= 0.0 ? state.preferredCaretX : caretX(lines[li], state.caret);
        const size_t target = event.key == SDLK_UP ? (li > 0 ? li - 1 : 0) : std::min(lines.size() - 1, li + 1);
        state.caret = hitTextOffset(lines[target], currentX);
        state.preferredCaretX = currentX;
        if (!(event.mod & SDL_KMOD_SHIFT)) state.selectionAnchor = state.caret;
      } else if (multiLine && (event.key == SDLK_RETURN || event.key == SDLK_RETURN2)) {
        pushUndoSnapshot(text, state);
        eraseSelection(text, state);
        text.insert(state.caret, "\n");
        ++state.caret;
        state.selectionAnchor = state.caret;
        changed = true;
      }
    }

    for (const std::string& input : textInputEvents) {
      std::string insert = filteredText(input, options_.filter, multiLine);
      if (insert.empty()) continue;
      pushUndoSnapshot(text, state);
      eraseSelection(text, state);
      if (!multiLine && options_.maxLength > 0) {
        const size_t room = options_.maxLength > codepointCount(text) ? options_.maxLength - codepointCount(text) : 0;
        const auto offsets = utf8Offsets(insert);
        insert.resize(offsets[std::min(room, offsets.size() - 1)]);
      }
      text.insert(state.caret, insert);
      state.caret += insert.size();
      state.selectionAnchor = state.caret;
      state.preferredCaretX = -1.0;
      changed = true;
    }
  }

  visibleText = displayText();
  const std::vector<UI_TextInputLayoutLine>& renderLines =
      layoutLines(visibleText, font, textRect.w - (multiLine ? kScrollbarWidth + 4.0 : 0.0), multiLine, renderStyle.fontSize, state);
  const size_t caretLine = lineForCaret(renderLines, state.caret);
  if (multiLine) {
    if (!state.draggingScrollbar) {
      if (caretLine < state.scrollLine) state.scrollLine = caretLine;
      if (caretLine >= state.scrollLine + visibleLineCount) state.scrollLine = caretLine - visibleLineCount + 1;
    }
    const size_t maxScroll = renderLines.size() > visibleLineCount ? renderLines.size() - visibleLineCount : 0;
    state.scrollLine = std::min(state.scrollLine, maxScroll);
  } else {
    const double cx = caretX(renderLines[0], state.caret);
    if (cx - state.scrollX > textRect.w - 4.0) state.scrollX = cx - textRect.w + 4.0;
    if (cx - state.scrollX < 0.0) state.scrollX = std::max(0.0, cx - 2.0);
  }

  uint32_t fill = focusedTextInputId == id_ ? style.hoverColour : style.fillColour;
  const double corner = clampCorner(style.corner, state.rect);
  ctx.set_fill_style(BLRgba32(fill));
  ctx.fill_round_rect(BLRoundRect(state.rect.x, state.rect.y, state.rect.w, state.rect.h, corner));
  ctx.set_stroke_style(BLRgba32(focusedTextInputId == id_ ? style.pressedColour : style.strokeColour));
  ctx.set_stroke_width(std::max(1.0, style.strokeWidth));
  ctx.stroke_round_rect(BLRoundRect(state.rect.x + 0.5, state.rect.y + 0.5, state.rect.w - 1.0, state.rect.h - 1.0, corner));

  BLContextCookie cookie;
  ctx.save(cookie);
  ctx.clip_to_rect(textRect);
  const size_t firstLine = multiLine ? state.scrollLine : 0;
  const size_t lastLine = multiLine ? std::min(renderLines.size(), firstLine + visibleLineCount + 1) : 1;
  const size_t selA = std::min(state.caret, state.selectionAnchor);
  const size_t selB = std::max(state.caret, state.selectionAnchor);
  for (size_t i = firstLine; i < lastLine; ++i) {
    const UI_TextInputLayoutLine& line = renderLines[i];
    const double lineTop = textRect.y + (i - firstLine) * lineHeight;
    const double y = multiLine ? lineTop : textRect.y + std::max(0.0, (textRect.h - lineHeight) * 0.5);
    const double x = textRect.x - (multiLine ? 0.0 : state.scrollX);
    if (selA != selB && selB >= line.begin && selA <= line.end) {
      const double sx = x + caretX(line, std::max(selA, line.begin));
      const double ex = x + caretX(line, std::min(selB, line.end));
      ctx.set_fill_style(BLRgba32(0x663B82F6u));
      ctx.fill_rect(BLRect(sx, y + 2.0, std::max(2.0, ex - sx), lineHeight - 2.0));
    }
    if (line.end > line.begin) {
      const VisibleTextRun run = visibleTextRunForClip(visibleText, line, x, textRect.x, textRect.w);
      if (run.end > run.begin) {
        BLGlyphBuffer glyphs;
        glyphs.set_utf8_text(visibleText.data() + run.begin, run.end - run.begin);
        font.shape(glyphs);
        ctx.set_fill_style(BLRgba32(style.textColour));
        ctx.fill_glyph_run(BLPoint(run.x, y + fontMetrics.ascent + 2.0), font, glyphs.glyph_run());
      }
    }
  }
  if (text.empty() && !options_.placeholder.empty() && focusedTextInputId != id_) {
    ctx.set_fill_style(BLRgba32((style.textColour & 0x00FFFFFFu) | 0x77000000u));
    BLGlyphBuffer glyphs;
    glyphs.set_utf8_text(options_.placeholder.data(), options_.placeholder.size());
    font.shape(glyphs);
    const double y = multiLine ? textRect.y : textRect.y + std::max(0.0, (textRect.h - lineHeight) * 0.5);
    ctx.fill_glyph_run(BLPoint(textRect.x, y + fontMetrics.ascent + 2.0), font, glyphs.glyph_run());
  }
  if (focusedTextInputId == id_ && std::fmod(seconds, 1.0) < 0.55) {
    const UI_TextInputLayoutLine& line = renderLines[caretLine];
    const double cx = textRect.x - (multiLine ? 0.0 : state.scrollX) + caretX(line, state.caret);
    const double cy = multiLine
                          ? textRect.y + (caretLine - state.scrollLine) * lineHeight
                          : textRect.y + std::max(0.0, (textRect.h - lineHeight) * 0.5);
    ctx.set_stroke_style(BLRgba32(style.textColour));
    ctx.set_stroke_width(1.5);
    ctx.stroke_line(BLLine(cx, cy + 3.0, cx, cy + lineHeight - 3.0));
  }
  ctx.restore(cookie);

  if (multiLine && renderLines.size() > visibleLineCount) {
    const double trackH = textRect.h;
    const double thumbH = std::max(24.0, trackH * static_cast<double>(visibleLineCount) / renderLines.size());
    const double maxScroll = static_cast<double>(renderLines.size() - visibleLineCount);
    const double thumbY = textRect.y + (trackH - thumbH) * (maxScroll <= 0.0 ? 0.0 : static_cast<double>(state.scrollLine) / maxScroll);
    ctx.set_fill_style(BLRgba32(0x33000000u));
    ctx.fill_round_rect(BLRoundRect(scrollbarTrackX, textRect.y, kScrollbarWidth, trackH, 5.0));
    ctx.set_fill_style(BLRgba32(0xAA64748Bu));
    ctx.fill_round_rect(BLRoundRect(scrollbarTrackX, thumbY, kScrollbarWidth, thumbH, 5.0));
  }
  if (options_.resizable) {
    ctx.set_stroke_style(BLRgba32(0xAA64748Bu));
    ctx.set_stroke_width(1.0);
    ctx.stroke_line(BLLine(handleRect.x + 4.0, handleRect.y + 12.0, handleRect.x + 12.0, handleRect.y + 4.0));
    ctx.stroke_line(BLLine(handleRect.x + 8.0, handleRect.y + 12.0, handleRect.x + 12.0, handleRect.y + 8.0));
  }

  return changed;
}

}  // namespace Blend2DUI
