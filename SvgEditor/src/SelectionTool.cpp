#include "SvgEditor/SelectionTool.h"

#include <algorithm>

namespace SvgEditor {

void SelectionTool::clear() {
  ids_.clear();
  bounds_ = BLRect();
  dragMode_ = DragMode::None;
  dragStart_ = BLPoint();
  dragCurrent_ = BLPoint();
}

void SelectionTool::setSelection(std::vector<std::string> ids, const SvgDocument& document) {
  ids_ = std::move(ids);
  dragMode_ = DragMode::None;
  dragStart_ = BLPoint();
  dragCurrent_ = BLPoint();
  refreshBounds(document);
}

void SelectionTool::setSingleSelection(const std::string& id, const SvgDocument& document) {
  ids_.clear();
  if (!id.empty()) ids_.push_back(id);
  dragMode_ = DragMode::None;
  dragStart_ = BLPoint();
  dragCurrent_ = BLPoint();
  refreshBounds(document);
}

bool SelectionTool::contains(const std::string& id) const {
  return std::find(ids_.begin(), ids_.end(), id) != ids_.end();
}

void SelectionTool::refreshBounds(const SvgDocument& document) {
  bounds_ = document.selectionBounds(ids_);
}

void SelectionTool::beginMarquee(const BLPoint& sceneStart) {
  dragMode_ = DragMode::Marquee;
  dragStart_ = sceneStart;
  dragCurrent_ = sceneStart;
}

void SelectionTool::beginMove(const BLPoint& sceneStart) {
  dragMode_ = DragMode::MoveSelection;
  dragStart_ = sceneStart;
  dragCurrent_ = sceneStart;
}

void SelectionTool::updateDrag(const BLPoint& sceneCurrent) {
  dragCurrent_ = sceneCurrent;
}

void SelectionTool::endDrag() {
  dragMode_ = DragMode::None;
}

BLRect SelectionTool::marqueeRect() const {
  const double x = std::min(dragStart_.x, dragCurrent_.x);
  const double y = std::min(dragStart_.y, dragCurrent_.y);
  return BLRect(x,
                y,
                std::abs(dragCurrent_.x - dragStart_.x),
                std::abs(dragCurrent_.y - dragStart_.y));
}

BLPoint SelectionTool::dragDelta() const {
  return BLPoint(dragCurrent_.x - dragStart_.x, dragCurrent_.y - dragStart_.y);
}

}  // namespace SvgEditor
