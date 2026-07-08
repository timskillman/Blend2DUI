#pragma once

#include "SvgDocument.h"

#include <optional>
#include <string>
#include <vector>

namespace SvgEditor {

struct EditorSnapshot {
  SvgDocument document;
  std::vector<std::string> selectionIds;
  std::string filePath;
};

class UndoRedo {
 public:
  void reset(const EditorSnapshot& snapshot);
  void capture(const EditorSnapshot& snapshot);
  void replaceCurrent(const EditorSnapshot& snapshot);
  bool canUndo() const;
  bool canRedo() const;
  std::optional<EditorSnapshot> undo(const EditorSnapshot& current);
  std::optional<EditorSnapshot> redo(const EditorSnapshot& current);
  void markSaved();
  bool dirty() const;

 private:
  std::vector<EditorSnapshot> undoStack_;
  std::vector<EditorSnapshot> redoStack_;
  size_t savedUndoDepth_ = 0;
};

}  // namespace SvgEditor
