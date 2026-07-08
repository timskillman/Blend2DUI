#include "SvgEditor/GroupEdit.h"

namespace SvgEditor {

bool GroupEdit::groupSelection(SvgDocument& document, SelectionTool& selection) {
  if (selection.ids().size() < 2) return false;
  const std::string groupId = document.groupNodes(selection.ids());
  if (groupId.empty()) return false;
  selection.setSingleSelection(groupId, document);
  return true;
}

bool GroupEdit::ungroupSelection(SvgDocument& document, SelectionTool& selection) {
  if (selection.empty()) return false;
  const std::vector<std::string> newSelection = document.ungroupNodes(selection.ids());
  if (newSelection.empty()) return false;
  selection.setSelection(newSelection, document);
  return true;
}

}  // namespace SvgEditor
