#include "SvgEditor/GroupEdit.h"
#include "SvgEditor/SelectionTool.h"
#include "SvgEditor/SvgSceneIO.h"
#include "SvgEditor/UndoRedo.h"

#include <cmath>
#include <iostream>
#include <string>
#include <vector>

namespace {

using SvgEditor::EditorSnapshot;
using SvgEditor::GroupEdit;
using SvgEditor::SelectionTool;
using SvgEditor::SvgDocument;
using SvgEditor::UndoRedo;

EditorSnapshot makeSnapshot(const SvgDocument& document,
                            const SelectionTool& selection,
                            const std::string& filePath) {
  return EditorSnapshot{document, selection.ids(), filePath};
}

bool nearlyEqual(double a, double b) {
  return std::abs(a - b) <= 1.0e-9;
}

bool sameMatrix(const BLMatrix2D& a, const BLMatrix2D& b) {
  return nearlyEqual(a.m00, b.m00) &&
         nearlyEqual(a.m01, b.m01) &&
         nearlyEqual(a.m10, b.m10) &&
         nearlyEqual(a.m11, b.m11) &&
         nearlyEqual(a.m20, b.m20) &&
         nearlyEqual(a.m21, b.m21);
}

int fail(const std::string& message) {
  std::cerr << "FAIL: " << message << '\n';
  return 1;
}

SvgEditor::Node makeRectPathNode(const std::string& id, double x, double y, double size) {
  SvgEditor::Node node;
  node.id = id;
  node.type = SvgEditor::NodeType::Path;
  node.style.fill.kind = SvgEditor::PaintKind::Color;
  node.style.fill.color = BLRgba32(0xFF000000u);
  node.commands = {
      {SvgEditor::PathCommand::Type::MoveTo, BLPoint(x, y)},
      {SvgEditor::PathCommand::Type::LineTo, BLPoint(x + size, y)},
      {SvgEditor::PathCommand::Type::LineTo, BLPoint(x + size, y + size)},
      {SvgEditor::PathCommand::Type::LineTo, BLPoint(x, y + size)},
      {SvgEditor::PathCommand::Type::Close, BLPoint(), BLPoint(), BLPoint()},
  };
  node.path = SvgEditor::SvgDocument::commandsToPath(node.commands);
  return node;
}

SvgEditor::Node makeGroupNode(const std::string& id, SvgEditor::Node child) {
  SvgEditor::Node group;
  group.id = id;
  group.type = SvgEditor::NodeType::Group;
  group.children.push_back(std::move(child));
  return group;
}

}  // namespace

int main() {
  SvgDocument document;
  document.resetToA4Landscape();
  document.root().children.push_back(makeRectPathNode("shape-a", 0.0, 0.0, 20.0));
  document.root().children.push_back(makeRectPathNode("shape-b", 30.0, 0.0, 20.0));
  document.root().children.push_back(makeRectPathNode("shape-c", 60.0, 0.0, 20.0));

  SelectionTool selection;
  std::vector<std::string> allIds;
  allIds.reserve(document.root().children.size());
  for (const SvgEditor::Node& child : document.root().children) {
    allIds.push_back(child.id);
  }
  selection.setSelection(allIds, document);

  UndoRedo undoRedo;
  undoRedo.reset(makeSnapshot(document, selection, "generated-smoke"));

  if (!GroupEdit::groupSelection(document, selection)) return fail("groupSelection failed");
  undoRedo.capture(makeSnapshot(document, selection, "generated-smoke"));

  if (selection.ids().size() != 1) return fail("groupSelection did not collapse to a single selected group");
  const std::string groupId = selection.ids().front();
  const auto undoAfterGroup = undoRedo.undo(makeSnapshot(document, selection, "generated-smoke"));
  if (!undoAfterGroup) return fail("undo unavailable after grouping");
  document = undoAfterGroup->document;
  selection.setSelection(undoAfterGroup->selectionIds, document);

  if (document.root().children.size() != allIds.size()) return fail("undo after grouping did not restore root child count");
  if (selection.ids().size() != allIds.size()) return fail("undo after grouping did not restore selection ids");

  const auto redoAfterGroup = undoRedo.redo(makeSnapshot(document, selection, "generated-smoke"));
  if (!redoAfterGroup) return fail("redo unavailable after undoing group");
  document = redoAfterGroup->document;
  selection.setSelection(redoAfterGroup->selectionIds, document);

  if (selection.ids().size() != 1 || selection.ids().front() != groupId) return fail("redo after grouping did not restore grouped selection");

  const BLMatrix2D originalTransform = document.findNode(groupId)->transform;
  if (!document.applyWorldTransform(selection.ids(), BLMatrix2D::make_translation(10.0, 15.0))) {
    return fail("applyWorldTransform failed on grouped selection");
  }
  undoRedo.capture(makeSnapshot(document, selection, "generated-smoke"));
  const BLMatrix2D movedTransform = document.findNode(groupId)->transform;
  if (sameMatrix(originalTransform, movedTransform)) return fail("group transform did not change after translation");

  const auto undoAfterMove = undoRedo.undo(makeSnapshot(document, selection, "generated-smoke"));
  if (!undoAfterMove) return fail("undo unavailable after move");
  document = undoAfterMove->document;
  selection.setSelection(undoAfterMove->selectionIds, document);
  const SvgEditor::Node* undoneGroup = document.findNode(groupId);
  if (!undoneGroup) return fail("group node missing after undoing move");
  if (!sameMatrix(undoneGroup->transform, originalTransform)) return fail("undo after move did not restore original transform");

  const auto redoAfterMove = undoRedo.redo(makeSnapshot(document, selection, "generated-smoke"));
  if (!redoAfterMove) return fail("redo unavailable after undoing move");
  document = redoAfterMove->document;
  selection.setSelection(redoAfterMove->selectionIds, document);
  const SvgEditor::Node* redoneGroup = document.findNode(groupId);
  if (!redoneGroup) return fail("group node missing after redoing move");
  if (!sameMatrix(redoneGroup->transform, movedTransform)) return fail("redo after move did not restore moved transform");

  SvgDocument mergeBase;
  mergeBase.resetToA4Landscape();
  mergeBase.root().children.push_back(makeGroupNode("group-1", makeRectPathNode("path-1", 0.0, 0.0, 20.0)));

  SvgDocument incoming;
  incoming.resetToA4Landscape();
  incoming.root().children.push_back(makeGroupNode("incoming-group", makeRectPathNode("incoming-path", 100.0, 0.0, 20.0)));

  const std::vector<std::string> mergedIds = mergeBase.mergeFrom(std::move(incoming));
  if (mergedIds.size() != 1) return fail("mergeFrom did not return a single merged root id");
  if (mergedIds.front() == "group-1") return fail("mergeFrom reused an existing destination node id");

  const SvgEditor::Node* mergedGroupBeforeMove = mergeBase.findNode(mergedIds.front());
  const SvgEditor::Node* existingGroupBeforeMove = mergeBase.findNode("group-1");
  if (!mergedGroupBeforeMove || !existingGroupBeforeMove) return fail("expected both existing and merged groups after merge");

  const BLMatrix2D existingGroupOriginal = existingGroupBeforeMove->transform;
  const BLMatrix2D mergedGroupOriginal = mergedGroupBeforeMove->transform;
  if (!mergeBase.applyWorldTransform(mergedIds, BLMatrix2D::make_translation(25.0, 0.0))) {
    return fail("applyWorldTransform failed on merged selection");
  }

  const SvgEditor::Node* mergedGroupAfterMove = mergeBase.findNode(mergedIds.front());
  const SvgEditor::Node* existingGroupAfterMove = mergeBase.findNode("group-1");
  if (!mergedGroupAfterMove || !existingGroupAfterMove) return fail("group lookup failed after moving merged selection");
  if (sameMatrix(mergedGroupAfterMove->transform, mergedGroupOriginal)) {
    return fail("moving merged selection did not change the merged group transform");
  }
  if (!sameMatrix(existingGroupAfterMove->transform, existingGroupOriginal)) {
    return fail("moving merged selection changed an unrelated existing group");
  }

  std::cout << "PASS: svg editor undo/merge smoke test succeeded\n";
  return 0;
}
