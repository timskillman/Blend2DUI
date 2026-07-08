#include "SvgEditor/PointSelectionController.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <optional>

namespace SvgEditor {
namespace {

constexpr std::uint64_t kSelectionDoubleClickMs = 350;
constexpr double kSelectionDoubleClickDistanceSq = 36.0;

bool isDoubleClickAt(std::uint64_t previousTicks, const BLPoint& previousPoint, const BLPoint& currentPoint) {
  if (previousTicks == 0) return false;
  const std::uint64_t now = SDL_GetTicks();
  if (now < previousTicks || now - previousTicks > kSelectionDoubleClickMs) return false;
  const double dx = currentPoint.x - previousPoint.x;
  const double dy = currentPoint.y - previousPoint.y;
  return dx * dx + dy * dy <= kSelectionDoubleClickDistanceSq;
}

std::optional<std::string> firstPointEditableHit(const SvgDocument& document, const BLPoint& scenePoint) {
  for (const std::string& candidateId : document.hitTestSelectionCycle(scenePoint)) {
    const Node* candidate = document.findNode(candidateId);
    if (candidate && candidate->type == NodeType::Path && candidate->pointEditable) return candidateId;
  }
  return std::nullopt;
}

std::optional<BLPoint> anchorLocalPointForCommand(const PathCommand& command) {
  switch (command.type) {
    case PathCommand::Type::MoveTo:
    case PathCommand::Type::LineTo:
      return command.p1;
    case PathCommand::Type::QuadTo:
      return command.p2;
    case PathCommand::Type::CubicTo:
      return command.p3;
    case PathCommand::Type::Close:
      return std::nullopt;
  }
  return std::nullopt;
}

void strokeConnectorLine(BLContext& ctx, const BLPoint& from, const BLPoint& to) {
  BLPath connector;
  connector.move_to(from.x, from.y);
  connector.line_to(to.x, to.y);
  ctx.stroke_path(connector);
}

void fillAnchorHandle(BLContext& ctx, const BLPoint& screenPoint, bool selected) {
  ctx.set_fill_style(selected ? BLRgba32(0xFFDC2626u) : BLRgba32(0xFF0EA5E9u));
  ctx.fill_rect(BLRect(screenPoint.x - 4.0, screenPoint.y - 4.0, 8.0, 8.0));
}

void fillControlHandle(BLContext& ctx, const BLPoint& screenPoint) {
  const BLRect rect(screenPoint.x - 3.0, screenPoint.y - 3.0, 6.0, 6.0);
  ctx.set_fill_style(BLRgba32(0xFFFFFFFFu));
  ctx.fill_rect(rect);
  ctx.set_stroke_style(BLRgba32(0xFF0284C7u));
  ctx.set_stroke_width(1.0);
  ctx.stroke_rect(rect);
}

}  // namespace

void PointSelectionController::clear() {
  pointEdit_.clear();
  dragModified_ = false;
  lastClickTicks_ = 0;
  lastClickScreen_ = BLPoint();
}

bool PointSelectionController::selectionPointEditAllowed(const SvgDocument& document, const SelectionTool& selection) const {
  if (selection.ids().size() != 1) return false;
  const Node* node = document.findNode(selection.ids().front());
  return node && node->type == NodeType::Path && node->pointEditable;
}

bool PointSelectionController::deleteSelected(SvgDocument& document, SelectionTool& selection) {
  if (!selectionPointEditAllowed(document, selection) || !pointEdit_.hasSelectedAnchors()) return false;
  if (!pointEdit_.deleteSelected(document, selection.ids().front())) return false;
  selection.refreshBounds(document);
  return true;
}

PointSelectionResult PointSelectionController::handleInteraction(Blend2DUI::SceneRenderer& renderer,
                                                                 const BLPoint& mouseScreen,
                                                                 const BLPoint& mouseScene,
                                                                 const RenderState& renderState,
                                                                 SvgDocument& document,
                                                                 SelectionTool& selection,
                                                                 bool showAllBezierHandles,
                                                                 bool snapToGrid,
                                                                 double gridStepScene) {
  PointSelectionResult result;
  const bool ctrlDown = (SDL_GetModState() & SDL_KMOD_CTRL) != 0;

  if (renderer.mousePressed()) {
    dragModified_ = false;
    if (!selectionPointEditAllowed(document, selection)) {
      if (const std::optional<std::string> pointEditableHit = firstPointEditableHit(document, mouseScene)) {
        selection.setSingleSelection(*pointEditableHit, document);
        clear();
      } else {
        const std::string hitId = document.hitTestSelectable(mouseScene);
        if (!hitId.empty()) {
          selection.setSingleSelection(hitId, document);
          clear();
        }
      }
    } else {
      const std::string currentPointEditId = selection.ids().front();
      bool switchedPointEditPath = false;
      if (isDoubleClickAt(lastClickTicks_, lastClickScreen_, mouseScreen)) {
        const std::vector<std::string> cycleIds = document.hitTestSelectionCycle(mouseScene);
        if (!cycleIds.empty()) {
          std::string nextPointEditId;
          const auto current = std::find(cycleIds.begin(), cycleIds.end(), currentPointEditId);
          if (current != cycleIds.end()) {
            const size_t currentIndex = static_cast<size_t>(std::distance(cycleIds.begin(), current));
            for (size_t offset = 1; offset < cycleIds.size(); ++offset) {
              const std::string& candidateId = cycleIds[(currentIndex + offset) % cycleIds.size()];
              const Node* candidate = document.findNode(candidateId);
              if (candidateId != currentPointEditId &&
                  candidate &&
                  candidate->type == NodeType::Path &&
                  candidate->pointEditable) {
                nextPointEditId = candidateId;
                break;
              }
            }
          } else {
            for (const std::string& candidateId : cycleIds) {
              const Node* candidate = document.findNode(candidateId);
              if (candidateId != currentPointEditId &&
                  candidate &&
                  candidate->type == NodeType::Path &&
                  candidate->pointEditable) {
                nextPointEditId = candidateId;
                break;
              }
            }
          }

          if (!nextPointEditId.empty()) {
            clear();
            selection.setSingleSelection(nextPointEditId, document);
            lastClickTicks_ = SDL_GetTicks();
            lastClickScreen_ = mouseScreen;
            switchedPointEditPath = true;
          }
        }
      }

      if (!switchedPointEditPath &&
          !pointEdit_.begin(document, currentPointEditId, mouseScreen, renderState, ctrlDown, showAllBezierHandles)) {
        pointEdit_.beginMarquee(mouseScreen, ctrlDown);
      }
      if (!switchedPointEditPath) {
        lastClickTicks_ = SDL_GetTicks();
        lastClickScreen_ = mouseScreen;
      }
    }
  }

  if (renderer.mouseDown()) {
    if (pointEdit_.active()) {
      if (pointEdit_.drag(document, mouseScreen, renderState, snapToGrid, gridStepScene)) {
        dragModified_ = true;
        selection.refreshBounds(document);
      }
    } else if (pointEdit_.marqueeActive()) {
      pointEdit_.updateMarquee(mouseScreen);
    }
  }

  if (renderer.mouseReleased()) {
    if (selectionPointEditAllowed(document, selection) && pointEdit_.marqueeActive()) {
      pointEdit_.endMarquee(document, selection.ids().front(), renderState, ctrlDown);
    }
    if (pointEdit_.active()) {
      pointEdit_.clearInteraction();
      if (dragModified_) {
        result.captureUndo = true;
        dragModified_ = false;
      }
    }
  }

  return result;
}

void PointSelectionController::renderOverlay(Blend2DUI::SceneRenderer& renderer,
                                             const SvgDocument& document,
                                             const SelectionTool& selection,
                                             const RenderState& renderState,
                                             bool showAllBezierHandles) const {
  if (!selectionPointEditAllowed(document, selection)) return;

  const std::string nodeId = selection.ids().front();
  const Node* node = document.findNode(nodeId);
  if (!node || node->type != NodeType::Path) return;

  BLContext& ctx = renderer.context();
  BLPath worldPath = node->path;
  worldPath.transform(document.worldTransformFor(nodeId));
  worldPath.transform(renderState.sceneToScreen);

  ctx.set_stroke_style(BLRgba32(0xFF0EA5E9u));
  ctx.set_stroke_width(2.0);
  ctx.stroke_path(worldPath);

  const BLMatrix2D world = document.worldTransformFor(nodeId);
  ctx.set_stroke_style(BLRgba32(0xB0CBD5E1u));
  ctx.set_stroke_width(1.0);
  std::vector<size_t> controlIndices = showAllBezierHandles ? std::vector<size_t>() : pointEdit_.selectedAnchors();
  if (showAllBezierHandles) {
    controlIndices.reserve(node->commands.size());
    for (size_t commandIndex = 0; commandIndex < node->commands.size(); ++commandIndex) {
      if (anchorLocalPointForCommand(node->commands[commandIndex])) controlIndices.push_back(commandIndex);
    }
  }

  for (size_t commandIndex : controlIndices) {
    if (commandIndex >= node->commands.size()) continue;
    const auto anchorLocal = anchorLocalPointForCommand(node->commands[commandIndex]);
    if (!anchorLocal) continue;

    const BLPoint anchorScreen = renderState.sceneToScreen.map_point(world.map_point(*anchorLocal));
    const PathCommand& command = node->commands[commandIndex];
    if (command.type == PathCommand::Type::QuadTo) {
      const BLPoint controlScreen = renderState.sceneToScreen.map_point(world.map_point(command.p1));
      strokeConnectorLine(ctx, anchorScreen, controlScreen);
      fillControlHandle(ctx, controlScreen);
    } else if (command.type == PathCommand::Type::CubicTo) {
      const BLPoint controlScreen = renderState.sceneToScreen.map_point(world.map_point(command.p2));
      strokeConnectorLine(ctx, anchorScreen, controlScreen);
      fillControlHandle(ctx, controlScreen);
    }

    if (commandIndex + 1 < node->commands.size()) {
      const PathCommand& next = node->commands[commandIndex + 1];
      if (next.type == PathCommand::Type::QuadTo || next.type == PathCommand::Type::CubicTo) {
        const BLPoint controlScreen = renderState.sceneToScreen.map_point(world.map_point(next.p1));
        strokeConnectorLine(ctx, anchorScreen, controlScreen);
        fillControlHandle(ctx, controlScreen);
      }
    }
  }

  if (pointEdit_.marqueeActive()) {
    const BLRect marquee = pointEdit_.marqueeRect();
    ctx.set_fill_style(BLRgba32(0x1AE11D48u));
    ctx.fill_rect(marquee);
    ctx.set_stroke_style(BLRgba32(0xFFE11D48u));
    ctx.set_stroke_width(1.0);
    ctx.stroke_rect(marquee);
  }

  for (size_t commandIndex = 0; commandIndex < node->commands.size(); ++commandIndex) {
    const auto anchorLocal = anchorLocalPointForCommand(node->commands[commandIndex]);
    if (!anchorLocal) continue;

    const BLPoint anchorScreen = renderState.sceneToScreen.map_point(world.map_point(*anchorLocal));
    fillAnchorHandle(ctx, anchorScreen, pointEdit_.isAnchorSelected(commandIndex));
  }
}

}  // namespace SvgEditor
