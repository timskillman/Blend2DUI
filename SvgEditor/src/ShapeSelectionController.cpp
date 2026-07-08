#include "SvgEditor/ShapeSelectionController.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <cmath>

namespace SvgEditor {
namespace {

constexpr std::uint64_t kSelectionDoubleClickMs = 350;
constexpr double kSelectionDoubleClickDistanceSq = 36.0;

double angleBetween(const BLPoint& origin, const BLPoint& point) {
  return std::atan2(point.y - origin.y, point.x - origin.x);
}

BLRect mapSceneRect(const RenderState& renderState, const BLRect& sceneRect) {
  const BLPoint p0 = renderState.sceneToScreen.map_point(BLPoint(sceneRect.x, sceneRect.y));
  const BLPoint p1 = renderState.sceneToScreen.map_point(BLPoint(sceneRect.x + sceneRect.w, sceneRect.y + sceneRect.h));
  return BLRect(std::min(p0.x, p1.x), std::min(p0.y, p1.y), std::abs(p1.x - p0.x), std::abs(p1.y - p0.y));
}

bool isDoubleClickAt(std::uint64_t previousTicks, const BLPoint& previousPoint, const BLPoint& currentPoint) {
  if (previousTicks == 0) return false;
  const std::uint64_t now = SDL_GetTicks();
  if (now < previousTicks || now - previousTicks > kSelectionDoubleClickMs) return false;
  const double dx = currentPoint.x - previousPoint.x;
  const double dy = currentPoint.y - previousPoint.y;
  return dx * dx + dy * dy <= kSelectionDoubleClickDistanceSq;
}

double dominantScaleFactor(double widthScale, double heightScale) {
  return std::abs(widthScale - 1.0) >= std::abs(heightScale - 1.0) ? widthScale : heightScale;
}

double snapValueToStep(double value, double step) {
  if (step <= 1.0e-6) return value;
  return std::round(value / step) * step;
}

std::vector<BLRect> selectionDetailBounds(const SvgDocument& document, const std::vector<std::string>& selectionIds) {
  std::vector<BLRect> bounds;
  if (selectionIds.size() <= 1) return bounds;

  bounds.reserve(selectionIds.size());
  for (const std::string& id : selectionIds) {
    BLRect nodeBounds;
    if (document.pathBounds(id, nodeBounds)) bounds.push_back(nodeBounds);
  }
  return bounds;
}

bool hitBelongsToSelection(const SvgDocument& document,
                           const SelectionTool& selection,
                           const std::string& hitId) {
  if (hitId.empty()) return false;
  if (selection.contains(hitId)) return true;
  for (const std::string& selectedId : selection.ids()) {
    if (document.isNodeDescendantOf(hitId, selectedId)) return true;
  }
  return false;
}

}  // namespace

void ShapeSelectionController::clear() {
  selection_.clear();
  clearInteractionState();
}

void ShapeSelectionController::setSelection(std::vector<std::string> ids, const SvgDocument& document) {
  selection_.setSelection(std::move(ids), document);
  clearInteractionState();
}

void ShapeSelectionController::setSingleSelection(const std::string& id, const SvgDocument& document) {
  selection_.setSingleSelection(id, document);
  clearInteractionState();
}

void ShapeSelectionController::refreshBounds(const SvgDocument& document) {
  selection_.refreshBounds(document);
}

ShapeSelectionResult ShapeSelectionController::handleInteraction(Blend2DUI::SceneRenderer& renderer,
                                                                 const BLPoint& mouseScreen,
                                                                 const BLPoint& mouseScene,
                                                                 const RenderState& renderState,
                                                                 SvgDocument& document,
                                                                 std::vector<Node>& clipboard,
                                                                 bool snapToGrid,
                                                                 double gridStepScene) {
  ShapeSelectionResult result;
  const BLRect selectionScreenBounds = mapSceneRect(renderState, selection_.bounds());
  const Transform::Handle handle = selection_.empty()
                                       ? Transform::Handle::None
                                       : Transform::hitTestHandle(selectionScreenBounds, mouseScreen);

  if (renderer.mousePressed()) {
    dragModified_ = false;
    if (handle == Transform::Handle::Delete && !selection_.empty()) {
      if (document.deleteNodes(selection_.ids())) {
        selection_.clear();
        clearInteractionState();
        result.captureUndo = true;
      }
    } else if (handle == Transform::Handle::Duplicate && !selection_.empty()) {
      clipboard = document.cloneNodes(selection_.ids());
      const std::vector<std::string> newIds = document.appendClonedNodes(clipboard, BLPoint(12.0, 12.0));
      selection_.setSelection(newIds, document);
      clearInteractionState();
      result.captureUndo = true;
    } else if ((handle == Transform::Handle::Rotate || handle == Transform::Handle::Scale) && !selection_.empty()) {
      transformDragActive_ = true;
      activeTransformHandle_ = handle;
      transformStartScene_ = mouseScene;
      transformStartBounds_ = selection_.bounds();
      transformAnchor_ = handle == Transform::Handle::Rotate
                             ? BLPoint(transformStartBounds_.x + transformStartBounds_.w * 0.5,
                                       transformStartBounds_.y + transformStartBounds_.h * 0.5)
                             : BLPoint(transformStartBounds_.x, transformStartBounds_.y);
    } else {
      const std::string hitId = document.hitTestSelectable(mouseScene);
      const bool hitWithinSelection = hitBelongsToSelection(document, selection_, hitId);
      if (!hitId.empty()) {
        if (isDoubleClickAt(lastClickTicks_, lastClickScreen_, mouseScreen)) {
          const std::vector<std::string> cycleIds = document.hitTestSelectionCycle(mouseScene);
          if (!cycleIds.empty()) {
            size_t nextIndex = 0;
            if (selection_.ids().size() == 1) {
              auto current = std::find(cycleIds.begin(), cycleIds.end(), selection_.ids().front());
              if (current != cycleIds.end()) {
                nextIndex = static_cast<size_t>(std::distance(cycleIds.begin(), current) + 1) % cycleIds.size();
              }
            }
            if (!hitWithinSelection || selection_.ids().size() == 1) {
              selection_.setSingleSelection(cycleIds[nextIndex], document);
            }
          } else if (!hitWithinSelection) {
            selection_.setSingleSelection(hitId, document);
          }
        } else if (!hitWithinSelection) {
          selection_.setSingleSelection(hitId, document);
        }
        lastClickTicks_ = SDL_GetTicks();
        lastClickScreen_ = mouseScreen;
        selection_.beginMove(mouseScene);
      } else {
        lastClickTicks_ = 0;
        lastClickScreen_ = BLPoint();
        selection_.clear();
        clearInteractionState();
        selection_.beginMarquee(mouseScene);
      }
    }
  }

  if (renderer.mouseDown()) {
    if (transformDragActive_ && !selection_.empty()) {
      if (activeTransformHandle_ == Transform::Handle::Rotate) {
        const double previousAngle = angleBetween(transformAnchor_, transformStartScene_);
        const double currentAngle = angleBetween(transformAnchor_, mouseScene);
        if (document.applyWorldTransform(selection_.ids(), Transform::makeRotation(currentAngle - previousAngle, transformAnchor_))) {
          dragModified_ = true;
          selection_.refreshBounds(document);
        }
        transformStartScene_ = mouseScene;
      } else if (activeTransformHandle_ == Transform::Handle::Scale) {
        const BLRect currentBounds = selection_.bounds();
        const double currentWidth = std::max(1.0e-4, currentBounds.w);
        const double currentHeight = std::max(1.0e-4, currentBounds.h);
        double targetWidth = std::max(1.0e-4, mouseScene.x - transformAnchor_.x);
        double targetHeight = std::max(1.0e-4, mouseScene.y - transformAnchor_.y);
        const bool freeScale = (SDL_GetModState() & SDL_KMOD_SHIFT) != 0;

        if (!freeScale) {
          const double baseWidth = std::max(1.0e-4, transformStartBounds_.w);
          const double baseHeight = std::max(1.0e-4, transformStartBounds_.h);
          const double uniformScale =
              dominantScaleFactor(targetWidth / baseWidth, targetHeight / baseHeight);
          targetWidth = std::max(1.0e-4, baseWidth * uniformScale);
          targetHeight = std::max(1.0e-4, baseHeight * uniformScale);
        }

        if (document.applyWorldTransform(selection_.ids(),
                                         Transform::makeScale(transformAnchor_,
                                                              std::clamp(targetWidth / currentWidth, 0.1, 10.0),
                                                              std::clamp(targetHeight / currentHeight, 0.1, 10.0)))) {
          dragModified_ = true;
          selection_.refreshBounds(document);
        }
        transformStartScene_ = mouseScene;
      }
    } else if (selection_.moveActive() && !selection_.empty()) {
      const BLPoint previous = selection_.dragDelta();
      selection_.updateDrag(mouseScene);
      const BLPoint current = selection_.dragDelta();
      BLPoint delta(current.x - previous.x, current.y - previous.y);
      if (snapToGrid) {
        const BLRect bounds = selection_.bounds();
        const double targetX = snapValueToStep(bounds.x + delta.x, gridStepScene);
        const double targetY = snapValueToStep(bounds.y + delta.y, gridStepScene);
        delta = BLPoint(targetX - bounds.x, targetY - bounds.y);
      }
      if ((std::abs(delta.x) > 0.0 || std::abs(delta.y) > 0.0) &&
          document.applyWorldTransform(selection_.ids(), Transform::makeTranslation(delta.x, delta.y))) {
        dragModified_ = true;
        selection_.refreshBounds(document);
      }
    } else if (selection_.marqueeActive()) {
      selection_.updateDrag(mouseScene);
    }
  }

  if (renderer.mouseReleased()) {
    if (selection_.marqueeActive()) {
      selection_.setSelection(document.marqueeSelect(selection_.marqueeRect()), document);
      selection_.endDrag();
    } else if (selection_.moveActive()) {
      selection_.endDrag();
    }
    if (transformDragActive_) {
      transformDragActive_ = false;
      activeTransformHandle_ = Transform::Handle::None;
    }
    if (dragModified_) {
      result.captureUndo = true;
      dragModified_ = false;
    }
  }

  return result;
}

void ShapeSelectionController::renderOverlay(Blend2DUI::SceneRenderer& renderer,
                                             const SvgDocument& document,
                                             const RenderState& renderState,
                                             const Blend2DUI::UI_ButtonStyleDefinition& handleButtonStyle,
                                             std::string_view handleIconAssetPath) {
  BLContext& ctx = renderer.context();

  if (selection_.marqueeActive()) {
    const BLRect marquee = mapSceneRect(renderState, selection_.marqueeRect());
    ctx.set_fill_style(BLRgba32(0x2A2563EBu));
    ctx.fill_rect(marquee);
    ctx.set_stroke_style(BLRgba32(0xFF2563EBu));
    ctx.set_stroke_width(1.0);
    ctx.stroke_rect(marquee);
  }

  if (selection_.empty()) return;

  const std::vector<BLRect> detailBounds = selectionDetailBounds(document, selection_.ids());
  ctx.set_stroke_style(BLRgba32(0xFFCBD5E1u));
  ctx.set_stroke_width(1.0);
  for (const BLRect& detailBoundsRect : detailBounds) {
    ctx.stroke_rect(mapSceneRect(renderState, detailBoundsRect));
  }

  const BLRect bounds = mapSceneRect(renderState, selection_.bounds());
  ctx.set_stroke_style(BLRgba32(0xFF2563EBu));
  ctx.set_stroke_width(1.4);
  ctx.stroke_rect(bounds);

  const Transform::HandleRects handles = Transform::handleRects(bounds);
  const std::array<BLRect, 4> buttonRects = {{
      handles.deleteRect,
      handles.rotateRect,
      handles.duplicateRect,
      handles.scaleRect,
  }};

  for (size_t i = 0; i < buttonRects.size(); ++i) {
    const std::string handleId = "selection-handle-" + std::to_string(i);
    renderer.UI_Button(handleId,
                       buttonRects[i],
                       handleButtonStyle,
                       Blend2DUI::UI_ButtonContent("", "", handleIconAssetPath));
  }
}

void ShapeSelectionController::clearInteractionState() {
  transformDragActive_ = false;
  activeTransformHandle_ = Transform::Handle::None;
  transformAnchor_ = BLPoint();
  transformStartScene_ = BLPoint();
  transformStartBounds_ = BLRect();
  dragModified_ = false;
  lastClickTicks_ = 0;
  lastClickScreen_ = BLPoint();
}

}  // namespace SvgEditor
