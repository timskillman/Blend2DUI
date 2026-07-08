#include "SvgEditor/UndoRedo.h"

namespace SvgEditor {

void UndoRedo::reset(const EditorSnapshot& snapshot) {
  undoStack_.clear();
  redoStack_.clear();
  undoStack_.push_back(snapshot);
  savedUndoDepth_ = undoStack_.size();
}

void UndoRedo::capture(const EditorSnapshot& snapshot) {
  undoStack_.push_back(snapshot);
  redoStack_.clear();
}

void UndoRedo::replaceCurrent(const EditorSnapshot& snapshot) {
  if (undoStack_.empty()) {
    undoStack_.push_back(snapshot);
  } else {
    undoStack_.back() = snapshot;
  }
}

bool UndoRedo::canUndo() const {
  return undoStack_.size() > 1;
}

bool UndoRedo::canRedo() const {
  return !redoStack_.empty();
}

std::optional<EditorSnapshot> UndoRedo::undo(const EditorSnapshot& current) {
  if (!canUndo()) return std::nullopt;
  redoStack_.push_back(current);
  undoStack_.pop_back();
  return undoStack_.back();
}

std::optional<EditorSnapshot> UndoRedo::redo(const EditorSnapshot& current) {
  if (!canRedo()) return std::nullopt;
  undoStack_.push_back(redoStack_.back());
  redoStack_.pop_back();
  return undoStack_.back();
}

void UndoRedo::markSaved() {
  savedUndoDepth_ = undoStack_.size();
}

bool UndoRedo::dirty() const {
  return undoStack_.size() != savedUndoDepth_;
}

}  // namespace SvgEditor
