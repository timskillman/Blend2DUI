#pragma once

#include "PointEdit.h"
#include "SelectionTool.h"

#include "SceneRenderer.h"

#include <cstdint>

namespace SvgEditor {

struct PointSelectionResult {
  bool captureUndo = false;
};

class PointSelectionController {
 public:
  void clear();
  bool interactionActive() const { return pointEdit_.active() || pointEdit_.marqueeActive(); }
  bool selectionPointEditAllowed(const SvgDocument& document, const SelectionTool& selection) const;
  bool hasSelectedAnchors() const { return pointEdit_.hasSelectedAnchors(); }
  bool deleteSelected(SvgDocument& document, SelectionTool& selection);

  PointSelectionResult handleInteraction(Blend2DUI::SceneRenderer& renderer,
                                         const BLPoint& mouseScreen,
                                         const BLPoint& mouseScene,
                                         const RenderState& renderState,
                                         SvgDocument& document,
                                         SelectionTool& selection,
                                         bool showAllBezierHandles,
                                         bool snapToGrid,
                                         double gridStepScene);
  void renderOverlay(Blend2DUI::SceneRenderer& renderer,
                     const SvgDocument& document,
                     const SelectionTool& selection,
                     const RenderState& renderState,
                     bool showAllBezierHandles) const;

 private:
  PointEdit pointEdit_;
  bool dragModified_ = false;
  std::uint64_t lastClickTicks_ = 0;
  BLPoint lastClickScreen_{};
};

}  // namespace SvgEditor
