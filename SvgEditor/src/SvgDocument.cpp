#include "SvgEditor/SvgDocument.h"

#include "Utility.h"
#include "SvgRender/SvgRenderer.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdlib>
#include <cmath>
#include <filesystem>
#include <functional>
#include <sstream>
#include <utility>

namespace SvgEditor {
namespace {

using Blend2DUI::contains;
using Blend2DUI::lower;
using Blend2DUI::trim;

BLRect rectFromBox(const BLBox& box) {
  return BLRect(box.x0, box.y0, std::max(0.0, box.x1 - box.x0), std::max(0.0, box.y1 - box.y0));
}

constexpr double kA4LandscapeWidth = 297.0;
constexpr double kA4LandscapeHeight = 210.0;

BLMatrix2D mediaFitTransform(double viewBoxX, double viewBoxY, double width, double height) {
  const double contentScale = std::min(kA4LandscapeWidth / std::max(1.0, width),
                                       kA4LandscapeHeight / std::max(1.0, height));
  const double contentTx = (kA4LandscapeWidth - width * contentScale) * 0.5 - viewBoxX * contentScale;
  const double contentTy = (kA4LandscapeHeight - height * contentScale) * 0.5 - viewBoxY * contentScale;
  return BLMatrix2D(contentScale, 0.0, 0.0, contentScale, contentTx, contentTy);
}

uint8_t clampByte(double value) {
  return static_cast<uint8_t>(std::max(0.0, std::min(255.0, std::round(value))));
}

BLRgba32 withAlpha(BLRgba32 color, double alpha) {
  const uint32_t value = color.value;
  const uint8_t a = clampByte(static_cast<double>((value >> 24) & 0xFFu) * alpha);
  return BLRgba32((uint32_t(a) << 24) | (value & 0x00FFFFFFu));
}

std::vector<uint8_t> decodeBase64(const std::string& input) {
  static const std::string alphabet =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::array<int, 256> table;
  table.fill(-1);
  for (size_t i = 0; i < alphabet.size(); ++i) {
    table[static_cast<unsigned char>(alphabet[i])] = static_cast<int>(i);
  }

  std::vector<uint8_t> output;
  int value = 0;
  int bits = -8;
  for (unsigned char c : input) {
    if (std::isspace(c)) continue;
    if (c == '=') break;
    const int decoded = table[c];
    if (decoded < 0) continue;
    value = (value << 6) | decoded;
    bits += 6;
    if (bits >= 0) {
      output.push_back(static_cast<uint8_t>((value >> bits) & 0xFF));
      bits -= 8;
    }
  }
  return output;
}

double toNumber(const std::string& value, double fallback = 0.0, double percentBase = 1.0) {
  const std::string text = trim(value);
  if (text.empty()) return fallback;
  char* end = nullptr;
  const double result = std::strtod(text.c_str(), &end);
  if (end && *end == '%') return result * percentBase / 100.0;
  return end == text.c_str() ? fallback : result;
}

std::vector<double> parseNumberList(const std::string& text) {
  std::vector<double> values;
  const char* cursor = text.c_str();
  while (*cursor) {
    while (*cursor && (std::isspace(static_cast<unsigned char>(*cursor)) || *cursor == ',')) ++cursor;
    if (!*cursor) break;
    char* end = nullptr;
    const double value = std::strtod(cursor, &end);
    if (end == cursor) {
      ++cursor;
    } else {
      values.push_back(value);
      cursor = end;
    }
  }
  return values;
}

void applyTransformString(BLContext& ctx, const std::string& text) {
  size_t pos = 0;
  while (pos < text.size()) {
    while (pos < text.size() && std::isspace(static_cast<unsigned char>(text[pos]))) ++pos;
    const size_t open = text.find('(', pos);
    if (open == std::string::npos) break;
    const size_t close = text.find(')', open + 1);
    if (close == std::string::npos) break;

    const std::string op = lower(trim(text.substr(pos, open - pos)));
    const std::vector<double> args = parseNumberList(text.substr(open + 1, close - open - 1));
    if (op == "translate" && !args.empty()) {
      ctx.translate(args[0], args.size() > 1 ? args[1] : 0.0);
    } else if (op == "scale" && !args.empty()) {
      ctx.scale(args[0], args.size() > 1 ? args[1] : args[0]);
    } else if (op == "rotate" && !args.empty()) {
      const double radians = args[0] * 3.14159265358979323846 / 180.0;
      if (args.size() >= 3) ctx.rotate(radians, args[1], args[2]);
      else ctx.rotate(radians);
    } else if (op == "matrix" && args.size() >= 6) {
      ctx.apply_transform(BLMatrix2D(args[0], args[1], args[2], args[3], args[4], args[5]));
    }
    pos = close + 1;
  }
}

std::filesystem::path resolveRelativePath(const std::string& basePath, const std::string& href) {
  namespace fs = std::filesystem;
  fs::path candidate(href);
  if (candidate.is_absolute()) return candidate;
  if (basePath.empty()) return candidate;
  return fs::path(basePath) / candidate;
}

std::string imageCacheKey(const std::string& sourceDirectory, const std::string& href) {
  return href.rfind("data:", 0) == 0 ? href : resolveRelativePath(sourceDirectory, href).string();
}

bool loadImageFromHref(const std::string& sourceDirectory, const std::string& href, BLImage& image) {
  if (href.rfind("data:", 0) == 0) {
    const size_t comma = href.find(',');
    if (comma == std::string::npos) return false;
    const std::string metadata = lower(href.substr(5, comma - 5));
    if (metadata.find(";base64") == std::string::npos) return false;

    const std::vector<uint8_t> bytes = decodeBase64(href.substr(comma + 1));
    return !bytes.empty() && image.read_from_data(bytes.data(), bytes.size()) == BL_SUCCESS;
  }

  const std::string resolved = resolveRelativePath(sourceDirectory, href).string();
  if (lower(std::filesystem::path(resolved).extension().string()) == ".svg") {
    SvgRenderOptions options;
    options.inputPath = resolved;
    return renderSvgToImage(options, image);
  }
  return image.read_from_file(resolved.c_str()) == BL_SUCCESS;
}

std::string defaultPrefixFor(const Node& node) {
  switch (node.type) {
    case NodeType::Group: return "group";
    case NodeType::Path: return "path";
    case NodeType::Image: return "image";
  }
  return "node";
}

BLMatrix2D copyMatrix(const BLMatrix2D& matrix) {
  return BLMatrix2D(matrix.m00, matrix.m01, matrix.m10, matrix.m11, matrix.m20, matrix.m21);
}

bool removeNodeRecursive(Node& parent, const std::string& id, Node& outNode) {
  for (auto it = parent.children.begin(); it != parent.children.end(); ++it) {
    if (it->id == id) {
      outNode = std::move(*it);
      parent.children.erase(it);
      return true;
    }
  }

  for (Node& child : parent.children) {
    if (removeNodeRecursive(child, id, outNode)) return true;
  }
  return false;
}

}  // namespace

SvgDocument::SvgDocument() {
  root_.id = "root";
  root_.type = NodeType::Group;
  root_.transform = BLMatrix2D::make_identity();
}

void SvgDocument::resetToA4Landscape() {
  root_.children.clear();
  gradients_.clear();
  patterns_.clear();
  patternImageCache_.clear();
  externalImageCache_.clear();
  sourcePath_.clear();
  nextNodeId_ = 1;
  setCanvas(0.0, 0.0, 297.0, 210.0);
}

void SvgDocument::setSourcePath(const std::string& path) {
  sourcePath_ = path;
  patternImageCache_.clear();
  externalImageCache_.clear();
}

std::string SvgDocument::sourceDirectory() const {
  namespace fs = std::filesystem;
  if (sourcePath_.empty()) return ".";
  const fs::path source(sourcePath_);
  return source.has_parent_path() ? source.parent_path().string() : ".";
}

void SvgDocument::setCanvas(double viewBoxX, double viewBoxY, double width, double height) {
  viewBoxX_ = viewBoxX;
  viewBoxY_ = viewBoxY;
  width_ = std::max(1.0, width);
  height_ = std::max(1.0, height);
}

RenderState SvgDocument::createRenderState(const BLRect& canvasRect, double zoom, const BLPoint& pan) const {
  constexpr double kMargin = 18.0;
  constexpr double kMaxRenderZoom = 320.0;
  const double availW = std::max(1.0, canvasRect.w - kMargin * 2.0);
  const double availH = std::max(1.0, canvasRect.h - kMargin * 2.0);
  const double clampedZoom = std::clamp(zoom, 0.1, kMaxRenderZoom);
  const double paperScale = std::min(availW / kA4LandscapeWidth, availH / kA4LandscapeHeight) * clampedZoom;
  const double paperTx = canvasRect.x + (canvasRect.w - kA4LandscapeWidth * paperScale) * 0.5 + pan.x;
  const double paperTy = canvasRect.y + (canvasRect.h - kA4LandscapeHeight * paperScale) * 0.5 + pan.y;
  const BLMatrix2D contentTransform = mediaFitTransform(viewBoxX_, viewBoxY_, width_, height_);

  RenderState state;
  state.scale = paperScale * contentTransform.m00;
  state.sceneToScreen = compose(BLMatrix2D(paperScale, 0.0, 0.0, paperScale, paperTx, paperTy),
                                contentTransform);
  state.screenToScene = copyMatrix(state.sceneToScreen);
  state.screenToScene.invert();
  state.paperRect = BLRect(paperTx, paperTy, kA4LandscapeWidth * paperScale, kA4LandscapeHeight * paperScale);
  return state;
}

void SvgDocument::render(BLContext& ctx, const BLRect& canvasRect, const RenderState& renderState) const {
  BLContextCookie cookie;
  ctx.save(cookie);
  ctx.clip_to_rect(canvasRect);

  ctx.set_fill_style(BLRgba32(0xFFF2F4F7u));
  ctx.fill_rect(canvasRect);

  ctx.set_fill_style(BLRgba32(0x1A0F172Au));
  ctx.fill_round_rect(BLRoundRect(renderState.paperRect.x + 8.0,
                                  renderState.paperRect.y + 10.0,
                                  renderState.paperRect.w,
                                  renderState.paperRect.h,
                                  8.0));
  ctx.set_fill_style(BLRgba32(0xFFFFFFFFu));
  ctx.fill_round_rect(BLRoundRect(renderState.paperRect.x,
                                  renderState.paperRect.y,
                                  renderState.paperRect.w,
                                  renderState.paperRect.h,
                                  8.0));
  ctx.set_stroke_style(BLRgba32(0xFFD0D7E2u));
  ctx.set_stroke_width(1.0);
  ctx.stroke_round_rect(BLRoundRect(renderState.paperRect.x + 0.5,
                                    renderState.paperRect.y + 0.5,
                                    renderState.paperRect.w - 1.0,
                                    renderState.paperRect.h - 1.0,
                                    8.0));

  ctx.apply_transform(renderState.sceneToScreen);
  for (const Node& child : root_.children) {
    renderNode(ctx, child);
  }
  ctx.restore(cookie);
}

std::string SvgDocument::createNodeId(const std::string& prefix) {
  for (;;) {
    const std::string candidate = prefix + "-" + std::to_string(nextNodeId_++);
    if (candidate == root_.id) continue;
    if (findNode(candidate)) continue;
    if (gradients_.find(candidate) != gradients_.end()) continue;
    if (patterns_.find(candidate) != patterns_.end()) continue;
    return candidate;
  }
}

void SvgDocument::makeDefinitionIdsUnique(const std::string& prefix) {
  std::unordered_map<std::string, std::string> remap;

  std::unordered_map<std::string, GradientDef> newGradients;
  for (auto& [id, gradient] : gradients_) {
    const std::string newId = prefix + id;
    remap[id] = newId;
    gradient.id = newId;
    newGradients[newId] = gradient;
  }
  gradients_ = std::move(newGradients);

  std::unordered_map<std::string, PatternDef> newPatterns;
  for (auto& [id, pattern] : patterns_) {
    const std::string newId = prefix + id;
    remap[id] = newId;
    pattern.id = newId;
    pattern.rawNode.attrs["id"] = newId;
    newPatterns[newId] = pattern;
  }
  patterns_ = std::move(newPatterns);

  const auto updatePaint = [&](Paint& paint) {
    if ((paint.kind == PaintKind::GradientRef || paint.kind == PaintKind::PatternRef) && remap.count(paint.refId)) {
      paint.refId = remap[paint.refId];
    }
  };

  std::function<void(Node&)> visit = [&](Node& node) {
    updatePaint(node.style.fill);
    updatePaint(node.style.stroke);
    for (Node& child : node.children) {
      visit(child);
    }
  };
  visit(root_);
}

std::vector<std::string> SvgDocument::mergeFrom(SvgDocument other) {
  std::vector<std::string> mergedIds;
  const std::string defPrefix = createNodeId("def") + "-";
  other.makeDefinitionIdsUnique(defPrefix);
  const BLMatrix2D sourceSceneToPaper = mediaFitTransform(other.viewBoxX_, other.viewBoxY_, other.width_, other.height_);
  BLMatrix2D paperToTargetScene = mediaFitTransform(viewBoxX_, viewBoxY_, width_, height_);
  if (!invertMatrix(paperToTargetScene, paperToTargetScene)) {
    paperToTargetScene = BLMatrix2D::make_identity();
  }
  const BLMatrix2D mergeTransform = compose(paperToTargetScene, sourceSceneToPaper);
  for (Node& child : other.root_.children) {
    assignFreshIds(child);
    child.transform = compose(mergeTransform, child.transform);
    mergedIds.push_back(child.id);
    root_.children.push_back(std::move(child));
  }
  for (auto& [id, gradient] : other.gradients_) {
    gradients_[id] = std::move(gradient);
  }
  for (auto& [id, pattern] : other.patterns_) {
    patterns_[id] = std::move(pattern);
  }
  return mergedIds;
}

Node* SvgDocument::findNode(const std::string& id) {
  return findNodeRecursive(root_, id);
}

const Node* SvgDocument::findNode(const std::string& id) const {
  return findNodeRecursive(root_, id);
}

bool SvgDocument::isNodeDescendantOf(const std::string& nodeId, const std::string& ancestorId) const {
  const Node* ancestor = findNode(ancestorId);
  return ancestor && containsNodeRecursive(*ancestor, nodeId);
}

BLMatrix2D SvgDocument::worldTransformFor(const std::string& id) const {
  BLMatrix2D world = BLMatrix2D::make_identity();
  BLMatrix2D parent = BLMatrix2D::make_identity();
  if (worldTransformRecursive(root_, BLMatrix2D::make_identity(), id, world, parent)) {
    return world;
  }
  return BLMatrix2D::make_identity();
}

BLMatrix2D SvgDocument::parentWorldTransformFor(const std::string& id) const {
  BLMatrix2D world = BLMatrix2D::make_identity();
  BLMatrix2D parent = BLMatrix2D::make_identity();
  if (worldTransformRecursive(root_, BLMatrix2D::make_identity(), id, world, parent)) {
    return parent;
  }
  return BLMatrix2D::make_identity();
}

std::string SvgDocument::hitTestSelectable(const BLPoint& scenePoint) const {
  for (auto it = root_.children.rbegin(); it != root_.children.rend(); ++it) {
    if (!hitTestNode(*it, BLMatrix2D::make_identity(), scenePoint).empty()) {
      return it->id;
    }
  }
  return {};
}

std::vector<std::string> SvgDocument::hitTestSelectionCycle(const BLPoint& scenePoint) const {
  std::vector<std::string> ids;
  const std::string topHit = hitTestSelectable(scenePoint);
  if (!topHit.empty()) ids.push_back(topHit);

  for (auto it = root_.children.rbegin(); it != root_.children.rend(); ++it) {
    collectHitLeafIds(*it, BLMatrix2D::make_identity(), scenePoint, ids);
  }
  return ids;
}

std::vector<std::string> SvgDocument::marqueeSelect(const BLRect& sceneRect) const {
  std::vector<std::string> ids;
  for (const Node& child : root_.children) {
    collectMarquee(child, BLMatrix2D::make_identity(), sceneRect, ids);
  }
  return ids;
}

BLRect SvgDocument::selectionBounds(const std::vector<std::string>& ids) const {
  BLRect bounds;
  bool initialized = false;
  for (const std::string& id : ids) {
    BLRect nodeRect;
    if (!pathBounds(id, nodeRect)) continue;
    bounds = initialized ? unionRect(bounds, nodeRect) : nodeRect;
    initialized = true;
  }
  return initialized ? bounds : BLRect();
}

bool SvgDocument::pathBounds(const std::string& id, BLRect& outBounds) const {
  const Node* node = findNode(id);
  if (!node) return false;
  return nodeBounds(*node, parentWorldTransformFor(id), outBounds);
}

std::vector<HandlePoint> SvgDocument::pointHandles(const std::string& id) const {
  std::vector<HandlePoint> handles;
  const Node* node = findNode(id);
  if (!node || node->type != NodeType::Path || node->commands.empty()) return handles;

  const BLMatrix2D world = worldTransformFor(id);
  for (size_t i = 0; i < node->commands.size(); ++i) {
    const PathCommand& command = node->commands[i];
    if (command.type == PathCommand::Type::MoveTo) {
      handles.push_back(HandlePoint{i, HandlePoint::Kind::MoveAnchor, world.map_point(command.p1)});
    } else if (command.type == PathCommand::Type::LineTo) {
      handles.push_back(HandlePoint{i, HandlePoint::Kind::Anchor, world.map_point(command.p1)});
    } else if (command.type == PathCommand::Type::QuadTo) {
      handles.push_back(HandlePoint{i, HandlePoint::Kind::Control1, world.map_point(command.p1)});
      handles.push_back(HandlePoint{i, HandlePoint::Kind::Anchor, world.map_point(command.p2)});
    } else if (command.type == PathCommand::Type::CubicTo) {
      handles.push_back(HandlePoint{i, HandlePoint::Kind::Control1, world.map_point(command.p1)});
      handles.push_back(HandlePoint{i, HandlePoint::Kind::Control2, world.map_point(command.p2)});
      handles.push_back(HandlePoint{i, HandlePoint::Kind::Anchor, world.map_point(command.p3)});
    }
  }
  return handles;
}

bool SvgDocument::applyWorldTransform(const std::vector<std::string>& ids, const BLMatrix2D& deltaWorld) {
  bool changed = false;
  for (const std::string& id : ids) {
    Node* node = findNode(id);
    if (!node) continue;
    BLMatrix2D parentWorld = parentWorldTransformFor(id);
    BLMatrix2D parentInverse = copyMatrix(parentWorld);
    if (parentInverse.invert() != BL_SUCCESS) continue;
    BLMatrix2D newTransform = compose(parentInverse, compose(deltaWorld, compose(parentWorld, node->transform)));
    node->transform = newTransform;
    changed = true;
  }
  return changed;
}

bool SvgDocument::updatePathCommands(const std::string& id, const std::vector<PathCommand>& commands) {
  Node* node = findNode(id);
  if (!node || node->type != NodeType::Path) return false;
  node->commands = commands;
  node->path = commandsToPath(commands);
  return true;
}

bool SvgDocument::deleteNodes(const std::vector<std::string>& ids) {
  bool changed = false;
  for (const std::string& id : ids) {
    Node removed;
    changed = removeNodeRecursive(root_, id, removed) || changed;
  }
  return changed;
}

std::vector<Node> SvgDocument::cloneNodes(const std::vector<std::string>& ids) const {
  std::vector<Node> cloned;
  for (const std::string& id : ids) {
    const Node* node = findNode(id);
    if (!node) continue;
    Node copy = *node;
    copy.transform = worldTransformFor(id);
    cloned.push_back(std::move(copy));
  }
  return cloned;
}

std::vector<Node> SvgDocument::extractNodes(const std::vector<std::string>& ids) {
  std::vector<Node> extracted;
  for (const std::string& id : ids) {
    const BLMatrix2D world = worldTransformFor(id);
    for (auto it = root_.children.begin(); it != root_.children.end(); ++it) {
      if (it->id == id) {
        Node node = std::move(*it);
        root_.children.erase(it);
        node.transform = world;
        extracted.push_back(std::move(node));
        goto next_id;
      }
    }
    {
      Node node;
      if (removeNodeRecursive(root_, id, node)) {
        node.transform = world;
        extracted.push_back(std::move(node));
      }
    }
  next_id:
    continue;
  }
  return extracted;
}

std::vector<std::string> SvgDocument::appendClonedNodes(std::vector<Node> nodes, const BLPoint& offset) {
  std::vector<std::string> ids;
  for (Node& node : nodes) {
    offsetNode(node, offset);
    assignFreshIds(node);
    ids.push_back(node.id);
    root_.children.push_back(std::move(node));
  }
  return ids;
}

std::string SvgDocument::groupNodes(const std::vector<std::string>& ids) {
  if (ids.size() < 2) return {};
  std::vector<Node> children = extractNodes(ids);
  if (children.size() < 2) return {};

  Node group;
  group.id = createNodeId("group");
  group.type = NodeType::Group;
  group.transform = BLMatrix2D::make_identity();
  group.children = std::move(children);
  root_.children.push_back(std::move(group));
  return root_.children.back().id;
}

std::vector<std::string> SvgDocument::ungroupNodes(const std::vector<std::string>& ids) {
  std::vector<std::string> newSelection;
  for (const std::string& id : ids) {
    const BLMatrix2D world = worldTransformFor(id);
    Node group;
    if (!removeNodeRecursive(root_, id, group)) {
      for (auto it = root_.children.begin(); it != root_.children.end(); ++it) {
        if (it->id == id) {
          group = std::move(*it);
          root_.children.erase(it);
          break;
        }
      }
    }
    if (group.type != NodeType::Group) continue;
    for (Node& child : group.children) {
      child.transform = compose(world, child.transform);
      newSelection.push_back(child.id);
      root_.children.push_back(std::move(child));
    }
  }
  return newSelection;
}

BLPath SvgDocument::commandsToPath(const std::vector<PathCommand>& commands) {
  BLPath path;
  for (const PathCommand& command : commands) {
    switch (command.type) {
      case PathCommand::Type::MoveTo:
        path.move_to(command.p1.x, command.p1.y);
        break;
      case PathCommand::Type::LineTo:
        path.line_to(command.p1.x, command.p1.y);
        break;
      case PathCommand::Type::QuadTo:
        path.quad_to(command.p1.x, command.p1.y, command.p2.x, command.p2.y);
        break;
      case PathCommand::Type::CubicTo:
        path.cubic_to(command.p1.x, command.p1.y, command.p2.x, command.p2.y, command.p3.x, command.p3.y);
        break;
      case PathCommand::Type::Close:
        path.close();
        break;
    }
  }
  return path;
}

std::vector<PathCommand> SvgDocument::pathToCommands(const BLPath& path) {
  std::vector<PathCommand> commands;
  const BLPathView view = path.view();
  for (size_t i = 0; i < view.size; ++i) {
    const uint8_t cmd = view.command_data[i];
    const BLPoint point = view.vertex_data[i];
    if (cmd == BL_PATH_CMD_MOVE) {
      commands.push_back(PathCommand{PathCommand::Type::MoveTo, point, {}, {}});
    } else if (cmd == BL_PATH_CMD_ON) {
      commands.push_back(PathCommand{PathCommand::Type::LineTo, point, {}, {}});
    } else if (cmd == BL_PATH_CMD_QUAD && i + 1 < view.size) {
      commands.push_back(PathCommand{PathCommand::Type::QuadTo, point, view.vertex_data[i + 1], {}});
      ++i;
    } else if (cmd == BL_PATH_CMD_CUBIC && i + 2 < view.size) {
      commands.push_back(PathCommand{PathCommand::Type::CubicTo, point, view.vertex_data[i + 1], view.vertex_data[i + 2]});
      i += 2;
    } else if (cmd == BL_PATH_CMD_CLOSE) {
      commands.push_back(PathCommand{PathCommand::Type::Close, {}, {}, {}});
    }
  }
  return commands;
}

BLMatrix2D SvgDocument::compose(const BLMatrix2D& parent, const BLMatrix2D& local) {
  BLMatrix2D result = copyMatrix(parent);
  result.transform(local);
  return result;
}

bool SvgDocument::invertMatrix(const BLMatrix2D& source, BLMatrix2D& inverse) {
  inverse = copyMatrix(source);
  return inverse.invert() == BL_SUCCESS;
}

BLRect SvgDocument::unionRect(const BLRect& a, const BLRect& b) {
  if (a.w <= 0.0 || a.h <= 0.0) return b;
  if (b.w <= 0.0 || b.h <= 0.0) return a;
  const double x0 = std::min(a.x, b.x);
  const double y0 = std::min(a.y, b.y);
  const double x1 = std::max(a.x + a.w, b.x + b.w);
  const double y1 = std::max(a.y + a.h, b.y + b.h);
  return BLRect(x0, y0, x1 - x0, y1 - y0);
}

bool SvgDocument::isIdentity(const BLMatrix2D& matrix) {
  return matrix == BLMatrix2D::make_identity();
}

std::string SvgDocument::matrixToSvg(const BLMatrix2D& matrix) {
  std::ostringstream out;
  out << "matrix("
      << matrix.m00 << ' '
      << matrix.m01 << ' '
      << matrix.m10 << ' '
      << matrix.m11 << ' '
      << matrix.m20 << ' '
      << matrix.m21 << ')';
  return out.str();
}

std::string SvgDocument::hitTestNode(const Node& node, const BLMatrix2D& parentWorld, const BLPoint& scenePoint) const {
  const BLMatrix2D world = compose(parentWorld, node.transform);
  if (node.type == NodeType::Group) {
    for (auto it = node.children.rbegin(); it != node.children.rend(); ++it) {
      const std::string childHit = hitTestNode(*it, world, scenePoint);
      if (!childHit.empty()) return it->type == NodeType::Group ? childHit : node.id;
    }
    return {};
  }

  const bool hit = node.type == NodeType::Path ? pathHitTest(node, scenePoint) : imageHitTest(node, scenePoint);
  return hit ? node.id : std::string();
}

void SvgDocument::collectHitLeafIds(const Node& node,
                                    const BLMatrix2D& parentWorld,
                                    const BLPoint& scenePoint,
                                    std::vector<std::string>& outIds) const {
  const BLMatrix2D world = compose(parentWorld, node.transform);
  if (node.type == NodeType::Group) {
    for (auto it = node.children.rbegin(); it != node.children.rend(); ++it) {
      collectHitLeafIds(*it, world, scenePoint, outIds);
    }
    return;
  }

  const bool hit = node.type == NodeType::Path ? pathHitTest(node, scenePoint) : imageHitTest(node, scenePoint);
  if (!hit) return;
  if (std::find(outIds.begin(), outIds.end(), node.id) == outIds.end()) outIds.push_back(node.id);
}

bool SvgDocument::nodeBounds(const Node& node, const BLMatrix2D& parentWorld, BLRect& outBounds) const {
  const BLMatrix2D world = compose(parentWorld, node.transform);
  if (node.type == NodeType::Group) {
    bool hasBounds = false;
    BLRect combined;
    for (const Node& child : node.children) {
      BLRect childBounds;
      if (!nodeBounds(child, world, childBounds)) continue;
      combined = hasBounds ? unionRect(combined, childBounds) : childBounds;
      hasBounds = true;
    }
    if (hasBounds) outBounds = combined;
    return hasBounds;
  }

  if (node.type == NodeType::Path) {
    BLPath path = node.path;
    path.transform(world);
    BLBox box;
    if (path.get_bounding_box(&box) != BL_SUCCESS) return false;
    outBounds = rectFromBox(box);
    return true;
  }

  const BLPoint p0 = world.map_point(BLPoint(node.image.x, node.image.y));
  const BLPoint p1 = world.map_point(BLPoint(node.image.x + node.image.width, node.image.y));
  const BLPoint p2 = world.map_point(BLPoint(node.image.x + node.image.width, node.image.y + node.image.height));
  const BLPoint p3 = world.map_point(BLPoint(node.image.x, node.image.y + node.image.height));
  const double minX = std::min(std::min(p0.x, p1.x), std::min(p2.x, p3.x));
  const double minY = std::min(std::min(p0.y, p1.y), std::min(p2.y, p3.y));
  const double maxX = std::max(std::max(p0.x, p1.x), std::max(p2.x, p3.x));
  const double maxY = std::max(std::max(p0.y, p1.y), std::max(p2.y, p3.y));
  outBounds = BLRect(minX, minY, maxX - minX, maxY - minY);
  return true;
}

void SvgDocument::collectMarquee(const Node& node,
                                 const BLMatrix2D& parentWorld,
                                 const BLRect& marqueeRect,
                                 std::vector<std::string>& outIds) const {
  BLRect bounds;
  if (!nodeBounds(node, parentWorld, bounds)) return;
  const bool intersects = bounds.x <= marqueeRect.x + marqueeRect.w &&
                          bounds.x + bounds.w >= marqueeRect.x &&
                          bounds.y <= marqueeRect.y + marqueeRect.h &&
                          bounds.y + bounds.h >= marqueeRect.y;
  if (!intersects) return;
  outIds.push_back(node.id);
}

void SvgDocument::renderNode(BLContext& ctx, const Node& node) const {
  BLContextCookie cookie;
  ctx.save(cookie);
  ctx.apply_transform(node.transform);
  if (node.type == NodeType::Group) {
    for (const Node& child : node.children) {
      renderNode(ctx, child);
    }
  } else if (node.type == NodeType::Path) {
    renderPathNode(ctx, node);
  } else {
    renderImageNode(ctx, node);
  }
  ctx.restore(cookie);
}

void SvgDocument::renderPathNode(BLContext& ctx, const Node& node) const {
  if (node.style.fill.kind != PaintKind::None) {
    setFillStyle(ctx, node.style);
    ctx.fill_path(node.path);
  }

  if (node.style.stroke.kind != PaintKind::None && node.style.strokeWidth > 0.0) {
    setStrokeStyle(ctx, node.style);
    if (!node.style.dashArray.empty()) {
      BLArray<double> dashes;
      for (double dash : node.style.dashArray) dashes.append(dash);
      ctx.set_stroke_dash_array(dashes);
      ctx.stroke_path(node.path);
      ctx.set_stroke_dash_array(BLArray<double>());
    } else {
      ctx.stroke_path(node.path);
    }
  }
}

void SvgDocument::renderImageNode(BLContext& ctx, const Node& node) const {
  if (node.image.href.empty()) return;

  const std::string key = imageCacheKey(sourceDirectory(), node.image.href);
  auto found = externalImageCache_.find(key);
  if (found == externalImageCache_.end()) {
    BLImage image;
    if (!loadImageFromHref(sourceDirectory(), node.image.href, image)) return;
    found = externalImageCache_.emplace(key, std::move(image)).first;
  }

  const double previousAlpha = ctx.global_alpha();
  ctx.set_global_alpha(previousAlpha * node.style.opacity);
  ctx.blit_image(BLRect(node.image.x, node.image.y, node.image.width, node.image.height), found->second);
  ctx.set_global_alpha(previousAlpha);
}

void SvgDocument::setFillStyle(BLContext& ctx, const Style& style) const {
  if (style.fill.kind == PaintKind::GradientRef) {
    if (auto pattern = patternFor(style.fill)) ctx.set_fill_style(*pattern);
    else ctx.set_fill_style(gradientFor(style.fill));
  } else if (style.fill.kind == PaintKind::PatternRef) {
    if (auto pattern = patternFor(style.fill)) ctx.set_fill_style(*pattern);
  } else {
    ctx.set_fill_style(withAlpha(style.fill.color, style.opacity * style.fillOpacity));
  }
}

void SvgDocument::setStrokeStyle(BLContext& ctx, const Style& style) const {
  if (style.stroke.kind == PaintKind::GradientRef) ctx.set_stroke_style(gradientFor(style.stroke));
  else ctx.set_stroke_style(withAlpha(style.stroke.color, style.opacity * style.strokeOpacity));
  ctx.set_stroke_width(style.strokeWidth);
  ctx.set_stroke_caps(style.lineCap);
  ctx.set_stroke_join(style.lineJoin);
}

std::optional<BLPattern> SvgDocument::patternFor(const Paint& paint) const {
  auto it = patterns_.find(paint.refId);
  if (it == patterns_.end()) return std::nullopt;

  const PatternDef& pattern = it->second;
  const std::string cacheKey = pattern.id;
  auto imageIt = patternImageCache_.find(cacheKey);
  if (imageIt == patternImageCache_.end()) {
    const int tileWidth = std::max(1, static_cast<int>(std::lround(pattern.width)));
    const int tileHeight = std::max(1, static_cast<int>(std::lround(pattern.height)));
    BLImage tile(tileWidth, tileHeight, BL_FORMAT_PRGB32);
    BLContext tileCtx(tile);
    tileCtx.set_comp_op(BL_COMP_OP_SRC_COPY);
    tileCtx.fill_all(BLRgba32(0x00000000u));
    tileCtx.set_comp_op(BL_COMP_OP_SRC_OVER);

    for (const XmlNode& child : pattern.rawNode.children) {
      if (lower(child.name) != "image") continue;
      auto hrefIt = child.attrs.find("href");
      if (hrefIt == child.attrs.end()) hrefIt = child.attrs.find("xlink:href");
      if (hrefIt == child.attrs.end()) continue;

      const std::string& href = hrefIt->second;
      const std::string key = imageCacheKey(sourceDirectory(), href);
      auto externalIt = externalImageCache_.find(key);
      if (externalIt == externalImageCache_.end()) {
        BLImage image;
        if (!loadImageFromHref(sourceDirectory(), href, image)) continue;
        externalIt = externalImageCache_.emplace(key, std::move(image)).first;
      }

      const double x = child.attrs.count("x") ? toNumber(child.attrs.at("x")) : 0.0;
      const double y = child.attrs.count("y") ? toNumber(child.attrs.at("y")) : 0.0;
      const double w = child.attrs.count("width") ? toNumber(child.attrs.at("width"), externalIt->second.width()) : externalIt->second.width();
      const double h = child.attrs.count("height") ? toNumber(child.attrs.at("height"), externalIt->second.height()) : externalIt->second.height();

      tileCtx.save();
      auto transformIt = child.attrs.find("transform");
      if (transformIt != child.attrs.end()) applyTransformString(tileCtx, transformIt->second);
      tileCtx.blit_image(BLRect(x, y, w, h), externalIt->second);
      tileCtx.restore();
    }

    tileCtx.end();
    imageIt = patternImageCache_.emplace(cacheKey, std::move(tile)).first;
  }

  return BLPattern(imageIt->second, BL_EXTEND_MODE_REPEAT, BLMatrix2D(1.0, 0.0, 0.0, 1.0, pattern.x, pattern.y));
}

BLGradient SvgDocument::gradientFor(const Paint& paint) const {
  auto it = gradients_.find(paint.refId);
  if (it == gradients_.end()) {
    return BLGradient(BLLinearGradientValues(viewBoxX_, viewBoxY_, viewBoxX_ + width_, viewBoxY_));
  }

  const GradientDef& gradientDef = it->second;
  BLGradient gradient = gradientDef.radial
                            ? BLGradient(BLRadialGradientValues(gradientDef.cx * width_,
                                                                gradientDef.cy * height_,
                                                                gradientDef.cx * width_,
                                                                gradientDef.cy * height_,
                                                                gradientDef.r * std::max(width_, height_)))
                            : BLGradient(BLLinearGradientValues(gradientDef.x1 * width_,
                                                                gradientDef.y1 * height_,
                                                                gradientDef.x2 * width_,
                                                                gradientDef.y2 * height_));
  for (const GradientStop& stop : gradientDef.stops) {
    gradient.add_stop(stop.offset, stop.color);
  }
  return gradient;
}

bool SvgDocument::pathHitTest(const Node& node, const BLPoint& scenePoint) const {
  BLPath world = node.path;
  world.transform(worldTransformFor(node.id));
  if (node.style.fill.kind != PaintKind::None &&
      world.hit_test(scenePoint, BL_FILL_RULE_NON_ZERO) != BL_HIT_TEST_OUT) {
    return true;
  }

  if (node.style.stroke.kind != PaintKind::None && node.style.strokeWidth > 0.0) {
    BLStrokeOptions options;
    options.width = node.style.strokeWidth;
    options.join = node.style.lineJoin;
    options.caps[BL_STROKE_CAP_POSITION_START] = node.style.lineCap;
    options.caps[BL_STROKE_CAP_POSITION_END] = node.style.lineCap;
    BLPath stroked;
    stroked.add_stroked_path(world, options, bl_default_approximation_options);
    return stroked.hit_test(scenePoint, BL_FILL_RULE_NON_ZERO) != BL_HIT_TEST_OUT;
  }
  return false;
}

bool SvgDocument::imageHitTest(const Node& node, const BLPoint& scenePoint) const {
  BLRect bounds;
  if (!pathBounds(node.id, bounds)) return false;
  return contains(bounds, scenePoint.x, scenePoint.y);
}

bool SvgDocument::extractNodesRecursive(Node& parent,
                                        const std::vector<std::string>& ids,
                                        std::vector<Node>& outNodes) {
  bool changed = false;
  for (auto it = parent.children.begin(); it != parent.children.end();) {
    if (std::find(ids.begin(), ids.end(), it->id) != ids.end()) {
      outNodes.push_back(std::move(*it));
      it = parent.children.erase(it);
      changed = true;
    } else {
      changed = extractNodesRecursive(*it, ids, outNodes) || changed;
      ++it;
    }
  }
  return changed;
}

bool SvgDocument::containsNodeRecursive(const Node& node, const std::string& id) const {
  if (node.id == id) return true;
  for (const Node& child : node.children) {
    if (containsNodeRecursive(child, id)) return true;
  }
  return false;
}

Node* SvgDocument::findNodeRecursive(Node& node, const std::string& id) {
  if (node.id == id) return &node;
  for (Node& child : node.children) {
    if (Node* found = findNodeRecursive(child, id)) return found;
  }
  return nullptr;
}

const Node* SvgDocument::findNodeRecursive(const Node& node, const std::string& id) const {
  if (node.id == id) return &node;
  for (const Node& child : node.children) {
    if (const Node* found = findNodeRecursive(child, id)) return found;
  }
  return nullptr;
}

bool SvgDocument::worldTransformRecursive(const Node& node,
                                          const BLMatrix2D& parentWorld,
                                          const std::string& id,
                                          BLMatrix2D& outWorld,
                                          BLMatrix2D& outParentWorld) const {
  const BLMatrix2D world = compose(parentWorld, node.transform);
  if (node.id == id) {
    outWorld = world;
    outParentWorld = parentWorld;
    return true;
  }
  for (const Node& child : node.children) {
    if (worldTransformRecursive(child, world, id, outWorld, outParentWorld)) return true;
  }
  return false;
}

void SvgDocument::assignFreshIds(Node& node) {
  node.id = createNodeId(defaultPrefixFor(node));
  for (Node& child : node.children) {
    assignFreshIds(child);
  }
}

void SvgDocument::collectCloneRecursive(const Node& node,
                                        const std::vector<std::string>& ids,
                                        std::vector<Node>& outNodes) const {
  if (std::find(ids.begin(), ids.end(), node.id) != ids.end()) {
    outNodes.push_back(node);
    return;
  }
  for (const Node& child : node.children) {
    collectCloneRecursive(child, ids, outNodes);
  }
}

void SvgDocument::offsetNode(Node& node, const BLPoint& offset) {
  node.transform.post_translate(offset);
}

}  // namespace SvgEditor
