#pragma once

#include "SelectionTool.h"
#include "Transform.h"

#include "SceneRenderer.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace SvgEditor {

struct ShapeSelectionResult {
  bool captureUndo = false;
};

class ShapeSelectionController {
 public:
  const SelectionTool& tool() const { return selection_; }
  SelectionTool& tool() { return selection_; }
  const std::vector<std::string>& ids() const { return selection_.ids(); }
  bool empty() const { return selection_.empty(); }
  bool contains(const std::string& id) const { return selection_.contains(id); }
  bool interactionActive() const {
    return selection_.marqueeActive() || selection_.moveActive() || transformDragActive_;
  }

  void clear();
  void setSelection(std::vector<std::string> ids, const SvgDocument& document);
  void setSingleSelection(const std::string& id, const SvgDocument& document);
  void refreshBounds(const SvgDocument& document);

  ShapeSelectionResult handleInteraction(Blend2DUI::SceneRenderer& renderer,
                                         const BLPoint& mouseScreen,
                                         const BLPoint& mouseScene,
                                         const RenderState& renderState,
                                         SvgDocument& document,
                                         std::vector<Node>& clipboard,
                                         bool snapToGrid,
                                         double gridStepScene);
  void renderOverlay(Blend2DUI::SceneRenderer& renderer,
                     const SvgDocument& document,
                     const RenderState& renderState,
                     const Blend2DUI::UI_ButtonStyleDefinition& handleButtonStyle,
                     std::string_view handleIconAssetPath);

 private:
  void clearInteractionState();

  SelectionTool selection_;
  bool transformDragActive_ = false;
  Transform::Handle activeTransformHandle_ = Transform::Handle::None;
  BLPoint transformAnchor_{};
  BLPoint transformStartScene_{};
  BLRect transformStartBounds_{};
  bool dragModified_ = false;
  std::uint64_t lastClickTicks_ = 0;
  BLPoint lastClickScreen_{};
};

}  // namespace SvgEditor
