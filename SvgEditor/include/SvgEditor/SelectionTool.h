#pragma once

#include "SvgDocument.h"

#include <string>
#include <vector>

namespace SvgEditor {

class SelectionTool {
 public:
  enum class DragMode {
    None,
    Marquee,
    MoveSelection
  };

  void clear();
  void setSelection(std::vector<std::string> ids, const SvgDocument& document);
  void setSingleSelection(const std::string& id, const SvgDocument& document);
  const std::vector<std::string>& ids() const { return ids_; }
  bool empty() const { return ids_.empty(); }
  bool contains(const std::string& id) const;
  const BLRect& bounds() const { return bounds_; }
  void refreshBounds(const SvgDocument& document);

  DragMode dragMode() const { return dragMode_; }
  void beginMarquee(const BLPoint& sceneStart);
  void beginMove(const BLPoint& sceneStart);
  void updateDrag(const BLPoint& sceneCurrent);
  void endDrag();

  bool marqueeActive() const { return dragMode_ == DragMode::Marquee; }
  bool moveActive() const { return dragMode_ == DragMode::MoveSelection; }
  BLRect marqueeRect() const;
  BLPoint dragDelta() const;

 private:
  std::vector<std::string> ids_;
  BLRect bounds_{};
  DragMode dragMode_ = DragMode::None;
  BLPoint dragStart_{};
  BLPoint dragCurrent_{};
};

}  // namespace SvgEditor
