#pragma once

#include "SvgDocument.h"

#include <optional>
#include <vector>

namespace SvgEditor {

class PointEdit {
 public:
  struct ActiveHandle {
    std::string nodeId;
    size_t commandIndex = 0;
    HandlePoint::Kind kind = HandlePoint::Kind::Anchor;
  };

  bool begin(const SvgDocument& document,
             const std::string& nodeId,
             const BLPoint& screenPoint,
             const RenderState& renderState,
             bool additiveSelection = false,
             bool showAllBezierHandles = false);
  bool active() const { return activeHandle_.has_value(); }
  const std::optional<ActiveHandle>& activeHandle() const { return activeHandle_; }
  bool marqueeActive() const { return marqueeActive_; }
  BLRect marqueeRect() const;
  void beginMarquee(const BLPoint& screenPoint, bool additiveSelection = false);
  void updateMarquee(const BLPoint& screenPoint);
  void endMarquee(const SvgDocument& document,
                  const std::string& nodeId,
                  const RenderState& renderState,
                  bool additiveSelection = false);
  const std::vector<size_t>& selectedAnchors() const { return selectedAnchorCommandIndices_; }
  bool hasSelectedAnchors() const { return !selectedAnchorCommandIndices_.empty(); }
  bool isAnchorSelected(size_t commandIndex) const;
  void clearInteraction();
  void clear();

  bool drag(SvgDocument& document,
            const BLPoint& screenPoint,
            const RenderState& renderState,
            bool snapToGrid = false,
            double gridStepScene = 0.0);
  bool deleteSelected(SvgDocument& document, const std::string& nodeId);

 private:
  void selectSingleAnchor(size_t commandIndex);
  void addAnchorToSelection(size_t commandIndex);

  std::optional<ActiveHandle> activeHandle_;
  BLPoint lastScenePoint_{};
  bool marqueeActive_ = false;
  bool marqueeAdditive_ = false;
  BLPoint marqueeStartScreen_{};
  BLPoint marqueeCurrentScreen_{};
  std::vector<size_t> selectedAnchorCommandIndices_;
};

}  // namespace SvgEditor
