#include "SvgEditor/PointEdit.h"

#include <algorithm>
#include <limits>
#include <unordered_set>

namespace SvgEditor {
namespace {

void offsetPoint(BLPoint& point, const BLPoint& delta) {
  point.x += delta.x;
  point.y += delta.y;
}

bool isAnchorKind(HandlePoint::Kind kind) {
  return kind == HandlePoint::Kind::MoveAnchor || kind == HandlePoint::Kind::Anchor;
}

bool isAnchorCommand(const PathCommand& command) {
  return command.type == PathCommand::Type::MoveTo ||
         command.type == PathCommand::Type::LineTo ||
         command.type == PathCommand::Type::QuadTo ||
         command.type == PathCommand::Type::CubicTo;
}

BLPoint anchorPointForCommand(const PathCommand& command) {
  switch (command.type) {
    case PathCommand::Type::MoveTo:
    case PathCommand::Type::LineTo:
      return command.p1;
    case PathCommand::Type::QuadTo:
      return command.p2;
    case PathCommand::Type::CubicTo:
      return command.p3;
    case PathCommand::Type::Close:
      return BLPoint();
  }
  return BLPoint();
}

std::optional<BLPoint> anchorLocalPointForCommand(const PathCommand& command) {
  return isAnchorCommand(command) ? std::optional<BLPoint>(anchorPointForCommand(command)) : std::nullopt;
}

BLPoint scenePointForKind(const PathCommand& command, HandlePoint::Kind kind) {
  switch (kind) {
    case HandlePoint::Kind::MoveAnchor:
    case HandlePoint::Kind::Anchor:
      return anchorPointForCommand(command);
    case HandlePoint::Kind::Control1:
      return command.p1;
    case HandlePoint::Kind::Control2:
      return command.p2;
  }
  return BLPoint();
}

void considerHandleCandidate(const std::string& nodeId,
                             size_t commandIndex,
                             HandlePoint::Kind kind,
                             const BLPoint& localPoint,
                             const BLMatrix2D& world,
                             const BLPoint& screenPoint,
                             const RenderState& renderState,
                             double& bestDistance,
                             std::optional<PointEdit::ActiveHandle>& bestHandle,
                             BLPoint& bestScenePoint) {
  const BLPoint screen = renderState.sceneToScreen.map_point(world.map_point(localPoint));
  const double dx = screen.x - screenPoint.x;
  const double dy = screen.y - screenPoint.y;
  const double distance = dx * dx + dy * dy;
  if (distance > 100.0 || distance >= bestDistance) return;
  bestDistance = distance;
  bestHandle = PointEdit::ActiveHandle{nodeId, commandIndex, kind};
  bestScenePoint = localPoint;
}

void setScenePointForKind(PathCommand& command, HandlePoint::Kind kind, const BLPoint& point) {
  switch (kind) {
    case HandlePoint::Kind::MoveAnchor:
      command.p1 = point;
      break;
    case HandlePoint::Kind::Anchor:
      if (command.type == PathCommand::Type::LineTo) command.p1 = point;
      else if (command.type == PathCommand::Type::QuadTo) command.p2 = point;
      else if (command.type == PathCommand::Type::CubicTo) command.p3 = point;
      break;
    case HandlePoint::Kind::Control1:
      command.p1 = point;
      break;
    case HandlePoint::Kind::Control2:
      command.p2 = point;
      break;
  }
}

void offsetIncomingControl(PathCommand& command, const BLPoint& delta) {
  if (command.type == PathCommand::Type::QuadTo) {
    offsetPoint(command.p1, delta);
  } else if (command.type == PathCommand::Type::CubicTo) {
    offsetPoint(command.p2, delta);
  }
}

void offsetOutgoingControl(PathCommand& command, const BLPoint& delta) {
  if (command.type == PathCommand::Type::QuadTo || command.type == PathCommand::Type::CubicTo) {
    offsetPoint(command.p1, delta);
  }
}

std::optional<size_t> previousAnchorIndex(const std::vector<PathCommand>& commands, size_t currentIndex) {
  if (currentIndex == 0) return std::nullopt;
  for (size_t i = currentIndex; i-- > 0;) {
    if (isAnchorCommand(commands[i])) return i;
  }
  return std::nullopt;
}

std::optional<size_t> nextAnchorIndex(const std::vector<PathCommand>& commands, size_t currentIndex) {
  for (size_t i = currentIndex + 1; i < commands.size(); ++i) {
    if (isAnchorCommand(commands[i])) return i;
  }
  return std::nullopt;
}

void applyAnchorDelta(std::vector<PathCommand>& commands,
                      size_t commandIndex,
                      const BLPoint& delta,
                      const std::unordered_set<size_t>& selectedAnchors) {
  if (commandIndex >= commands.size() || !isAnchorCommand(commands[commandIndex])) return;

  PathCommand& command = commands[commandIndex];
  setScenePointForKind(command,
                       command.type == PathCommand::Type::MoveTo ? HandlePoint::Kind::MoveAnchor : HandlePoint::Kind::Anchor,
                       BLPoint(anchorPointForCommand(command).x + delta.x, anchorPointForCommand(command).y + delta.y));

  if (command.type == PathCommand::Type::QuadTo) {
    const auto previousIndex = previousAnchorIndex(commands, commandIndex);
    if (!previousIndex || !selectedAnchors.count(*previousIndex)) offsetIncomingControl(command, delta);
  } else if (command.type == PathCommand::Type::CubicTo) {
    offsetIncomingControl(command, delta);
  }

  const auto nextIndex = nextAnchorIndex(commands, commandIndex);
  if (!nextIndex) return;

  PathCommand& next = commands[*nextIndex];
  if (next.type == PathCommand::Type::QuadTo) {
    if (!selectedAnchors.count(*nextIndex)) offsetOutgoingControl(next, delta);
  } else if (next.type == PathCommand::Type::CubicTo) {
    offsetOutgoingControl(next, delta);
  }
}

void dedupeAndSort(std::vector<size_t>& values) {
  std::sort(values.begin(), values.end());
  values.erase(std::unique(values.begin(), values.end()), values.end());
}

double snapValueToStep(double value, double step) {
  if (step <= 1.0e-6) return value;
  return std::round(value / step) * step;
}

}  // namespace

bool PointEdit::begin(const SvgDocument& document,
                      const std::string& nodeId,
                      const BLPoint& screenPoint,
                      const RenderState& renderState,
                      bool additiveSelection,
                      bool showAllBezierHandles) {
  const Node* node = document.findNode(nodeId);
  if (!node || node->type != NodeType::Path) return false;

  const BLMatrix2D world = document.worldTransformFor(nodeId);
  double bestDistance = std::numeric_limits<double>::max();
  std::optional<ActiveHandle> bestHandle;
  BLPoint bestScenePoint;

  marqueeActive_ = false;

  for (size_t commandIndex = 0; commandIndex < node->commands.size(); ++commandIndex) {
    const PathCommand& command = node->commands[commandIndex];
    if (!isAnchorCommand(command)) continue;

    considerHandleCandidate(nodeId,
                            commandIndex,
                            command.type == PathCommand::Type::MoveTo ? HandlePoint::Kind::MoveAnchor
                                                                      : HandlePoint::Kind::Anchor,
                            anchorPointForCommand(command),
                            world,
                            screenPoint,
                            renderState,
                            bestDistance,
                            bestHandle,
                            bestScenePoint);

    if (!(showAllBezierHandles || isAnchorSelected(commandIndex))) continue;

    if (command.type == PathCommand::Type::QuadTo) {
      considerHandleCandidate(nodeId,
                              commandIndex,
                              HandlePoint::Kind::Control1,
                              command.p1,
                              world,
                              screenPoint,
                              renderState,
                              bestDistance,
                              bestHandle,
                              bestScenePoint);
    } else if (command.type == PathCommand::Type::CubicTo) {
      considerHandleCandidate(nodeId,
                              commandIndex,
                              HandlePoint::Kind::Control2,
                              command.p2,
                              world,
                              screenPoint,
                              renderState,
                              bestDistance,
                              bestHandle,
                              bestScenePoint);
    }

    if (commandIndex + 1 < node->commands.size()) {
      const PathCommand& next = node->commands[commandIndex + 1];
      if (next.type == PathCommand::Type::QuadTo || next.type == PathCommand::Type::CubicTo) {
        considerHandleCandidate(nodeId,
                                commandIndex + 1,
                                HandlePoint::Kind::Control1,
                                next.p1,
                                world,
                                screenPoint,
                                renderState,
                                bestDistance,
                                bestHandle,
                                bestScenePoint);
      }
    }
  }

  activeHandle_ = bestHandle;
  lastScenePoint_ = bestScenePoint;
  if (!activeHandle_) return false;

  if (isAnchorKind(activeHandle_->kind) && !isAnchorSelected(activeHandle_->commandIndex)) {
    if (additiveSelection) addAnchorToSelection(activeHandle_->commandIndex);
    else selectSingleAnchor(activeHandle_->commandIndex);
  }
  return true;
}

BLRect PointEdit::marqueeRect() const {
  return BLRect(std::min(marqueeStartScreen_.x, marqueeCurrentScreen_.x),
                std::min(marqueeStartScreen_.y, marqueeCurrentScreen_.y),
                std::abs(marqueeCurrentScreen_.x - marqueeStartScreen_.x),
                std::abs(marqueeCurrentScreen_.y - marqueeStartScreen_.y));
}

void PointEdit::beginMarquee(const BLPoint& screenPoint, bool additiveSelection) {
  clearInteraction();
  marqueeActive_ = true;
  marqueeAdditive_ = additiveSelection;
  marqueeStartScreen_ = screenPoint;
  marqueeCurrentScreen_ = screenPoint;
}

void PointEdit::updateMarquee(const BLPoint& screenPoint) {
  if (!marqueeActive_) return;
  marqueeCurrentScreen_ = screenPoint;
}

void PointEdit::endMarquee(const SvgDocument& document,
                           const std::string& nodeId,
                           const RenderState& renderState,
                           bool additiveSelection) {
  if (!marqueeActive_) return;

  const Node* node = document.findNode(nodeId);
  if (!node || node->type != NodeType::Path) {
    marqueeActive_ = false;
    marqueeAdditive_ = false;
    return;
  }

  const BLRect rect = marqueeRect();
  const BLMatrix2D world = document.worldTransformFor(nodeId);
  std::vector<size_t> selected = additiveSelection || marqueeAdditive_ ? selectedAnchorCommandIndices_ : std::vector<size_t>();
  for (size_t commandIndex = 0; commandIndex < node->commands.size(); ++commandIndex) {
    const auto anchorLocal = anchorLocalPointForCommand(node->commands[commandIndex]);
    if (!anchorLocal) continue;
    const BLPoint screen = renderState.sceneToScreen.map_point(world.map_point(*anchorLocal));
    if (screen.x >= rect.x && screen.x <= rect.x + rect.w &&
        screen.y >= rect.y && screen.y <= rect.y + rect.h) {
      selected.push_back(commandIndex);
    }
  }
  selectedAnchorCommandIndices_ = std::move(selected);
  dedupeAndSort(selectedAnchorCommandIndices_);
  marqueeActive_ = false;
  marqueeAdditive_ = false;
}

bool PointEdit::isAnchorSelected(size_t commandIndex) const {
  return std::find(selectedAnchorCommandIndices_.begin(), selectedAnchorCommandIndices_.end(), commandIndex) !=
         selectedAnchorCommandIndices_.end();
}

void PointEdit::clearInteraction() {
  activeHandle_.reset();
  lastScenePoint_ = BLPoint();
  marqueeActive_ = false;
  marqueeAdditive_ = false;
  marqueeStartScreen_ = BLPoint();
  marqueeCurrentScreen_ = BLPoint();
}

void PointEdit::clear() {
  clearInteraction();
  selectedAnchorCommandIndices_.clear();
}

bool PointEdit::drag(SvgDocument& document,
                     const BLPoint& screenPoint,
                     const RenderState& renderState,
                     bool snapToGrid,
                     double gridStepScene) {
  if (!activeHandle_) return false;
  Node* node = document.findNode(activeHandle_->nodeId);
  if (!node || node->type != NodeType::Path) return false;

  BLMatrix2D world = document.worldTransformFor(activeHandle_->nodeId);
  BLMatrix2D inverse = world;
  if (inverse.invert() != BL_SUCCESS) return false;

  BLPoint scenePoint = renderState.screenToScene.map_point(screenPoint);
  if (snapToGrid) {
    scenePoint = BLPoint(snapValueToStep(scenePoint.x, gridStepScene),
                         snapValueToStep(scenePoint.y, gridStepScene));
  }
  const BLPoint localPoint = inverse.map_point(scenePoint);

  std::vector<PathCommand> commands = node->commands;
  if (activeHandle_->commandIndex >= commands.size()) return false;
  const BLPoint previousPoint = scenePointForKind(commands[activeHandle_->commandIndex], activeHandle_->kind);
  const BLPoint delta(localPoint.x - previousPoint.x, localPoint.y - previousPoint.y);
  if (delta.x == 0.0 && delta.y == 0.0) return false;

  if (isAnchorKind(activeHandle_->kind) && isAnchorSelected(activeHandle_->commandIndex) && !selectedAnchorCommandIndices_.empty()) {
    std::unordered_set<size_t> selectedAnchors(selectedAnchorCommandIndices_.begin(), selectedAnchorCommandIndices_.end());
    for (size_t commandIndex : selectedAnchorCommandIndices_) {
      applyAnchorDelta(commands, commandIndex, delta, selectedAnchors);
    }
  } else {
    setScenePointForKind(commands[activeHandle_->commandIndex], activeHandle_->kind, localPoint);
  }

  lastScenePoint_ = localPoint;
  return document.updatePathCommands(activeHandle_->nodeId, commands);
}

bool PointEdit::deleteSelected(SvgDocument& document, const std::string& nodeId) {
  if (selectedAnchorCommandIndices_.empty()) return false;
  Node* node = document.findNode(nodeId);
  if (!node || node->type != NodeType::Path) return false;

  std::vector<PathCommand> commands = node->commands;
  size_t anchorCount = 0;
  for (const PathCommand& command : commands) {
    if (isAnchorCommand(command)) ++anchorCount;
  }
  if (anchorCount <= selectedAnchorCommandIndices_.size()) return false;

  std::vector<size_t> indices = selectedAnchorCommandIndices_;
  dedupeAndSort(indices);
  for (auto it = indices.rbegin(); it != indices.rend(); ++it) {
    const size_t index = *it;
    if (index >= commands.size() || !isAnchorCommand(commands[index])) continue;

    if (index == 0) {
      const auto nextIndex = nextAnchorIndex(commands, 0);
      if (!nextIndex) continue;
      const BLPoint nextAnchor = anchorPointForCommand(commands[*nextIndex]);
      commands[*nextIndex].type = PathCommand::Type::MoveTo;
      commands[*nextIndex].p1 = nextAnchor;
      commands[*nextIndex].p2 = BLPoint();
      commands[*nextIndex].p3 = BLPoint();
    }
    commands.erase(commands.begin() + static_cast<std::ptrdiff_t>(index));
  }

  if (commands.empty() || commands.front().type != PathCommand::Type::MoveTo) return false;
  clear();
  return document.updatePathCommands(nodeId, commands);
}

void PointEdit::selectSingleAnchor(size_t commandIndex) {
  selectedAnchorCommandIndices_.assign(1, commandIndex);
}

void PointEdit::addAnchorToSelection(size_t commandIndex) {
  selectedAnchorCommandIndices_.push_back(commandIndex);
  dedupeAndSort(selectedAnchorCommandIndices_);
}

}  // namespace SvgEditor
