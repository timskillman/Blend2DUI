#pragma once

#include <blend2d/blend2d.h>

#include <map>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace SvgEditor {

enum class PaintKind {
  None,
  Color,
  GradientRef,
  PatternRef
};

struct Paint {
  PaintKind kind = PaintKind::None;
  BLRgba32 color = BLRgba32(0x00000000u);
  std::string refId;
};

struct Style {
  Paint fill;
  Paint stroke;
  double strokeWidth = 1.0;
  double opacity = 1.0;
  double fillOpacity = 1.0;
  double strokeOpacity = 1.0;
  double fontSize = 16.0;
  std::string fontFamily;
  std::vector<double> dashArray;
  BLStrokeCap lineCap = BL_STROKE_CAP_BUTT;
  BLStrokeJoin lineJoin = BL_STROKE_JOIN_MITER_CLIP;
};

struct GradientStop {
  double offset = 0.0;
  BLRgba32 color = BLRgba32(0xFF000000u);
};

struct GradientDef {
  std::string id;
  bool radial = false;
  double x1 = 0.0;
  double y1 = 0.0;
  double x2 = 1.0;
  double y2 = 0.0;
  double cx = 0.5;
  double cy = 0.5;
  double r = 0.5;
  bool percentUnits = true;
  std::vector<GradientStop> stops;
};

struct XmlNode {
  std::string name;
  std::map<std::string, std::string> attrs;
  std::vector<XmlNode> children;
  std::string text;
};

struct PatternDef {
  std::string id;
  double x = 0.0;
  double y = 0.0;
  double width = 0.0;
  double height = 0.0;
  XmlNode rawNode;
};

enum class NodeType {
  Group,
  Path,
  Image
};

struct PathCommand {
  enum class Type {
    MoveTo,
    LineTo,
    QuadTo,
    CubicTo,
    Close
  };

  Type type = Type::MoveTo;
  BLPoint p1{};
  BLPoint p2{};
  BLPoint p3{};
};

struct ImageData {
  std::string href;
  double x = 0.0;
  double y = 0.0;
  double width = 0.0;
  double height = 0.0;
};

struct Node {
  std::string id;
  NodeType type = NodeType::Group;
  BLMatrix2D transform = BLMatrix2D::make_identity();
  Style style;
  BLPath path;
  std::vector<PathCommand> commands;
  ImageData image;
  std::vector<Node> children;
  bool pointEditable = false;
};

struct RenderState {
  BLMatrix2D sceneToScreen = BLMatrix2D::make_identity();
  BLMatrix2D screenToScene = BLMatrix2D::make_identity();
  BLRect paperRect;
  double scale = 1.0;
};

struct HandlePoint {
  enum class Kind {
    MoveAnchor,
    Anchor,
    Control1,
    Control2
  };

  size_t commandIndex = 0;
  Kind kind = Kind::Anchor;
  BLPoint scenePoint{};
};

class SvgDocument {
 public:
  SvgDocument();

  void resetToA4Landscape();
  void setSourcePath(const std::string& path);
  const std::string& sourcePath() const { return sourcePath_; }
  std::string sourceDirectory() const;

  double viewBoxX() const { return viewBoxX_; }
  double viewBoxY() const { return viewBoxY_; }
  double width() const { return width_; }
  double height() const { return height_; }
  void setCanvas(double viewBoxX, double viewBoxY, double width, double height);

  Node& root() { return root_; }
  const Node& root() const { return root_; }
  std::unordered_map<std::string, GradientDef>& gradients() { return gradients_; }
  const std::unordered_map<std::string, GradientDef>& gradients() const { return gradients_; }
  std::unordered_map<std::string, PatternDef>& patterns() { return patterns_; }
  const std::unordered_map<std::string, PatternDef>& patterns() const { return patterns_; }

  RenderState createRenderState(const BLRect& canvasRect, double zoom, const BLPoint& pan) const;
  void render(BLContext& ctx, const BLRect& canvasRect, const RenderState& renderState) const;

  std::string createNodeId(const std::string& prefix);
  void makeDefinitionIdsUnique(const std::string& prefix);
  std::vector<std::string> mergeFrom(SvgDocument other);

  Node* findNode(const std::string& id);
  const Node* findNode(const std::string& id) const;
  bool isNodeDescendantOf(const std::string& nodeId, const std::string& ancestorId) const;
  BLMatrix2D worldTransformFor(const std::string& id) const;
  BLMatrix2D parentWorldTransformFor(const std::string& id) const;

  std::string hitTestSelectable(const BLPoint& scenePoint) const;
  std::vector<std::string> hitTestSelectionCycle(const BLPoint& scenePoint) const;
  std::vector<std::string> marqueeSelect(const BLRect& sceneRect) const;
  BLRect selectionBounds(const std::vector<std::string>& ids) const;
  bool pathBounds(const std::string& id, BLRect& outBounds) const;
  std::vector<HandlePoint> pointHandles(const std::string& id) const;

  bool applyWorldTransform(const std::vector<std::string>& ids, const BLMatrix2D& deltaWorld);
  bool updatePathCommands(const std::string& id, const std::vector<PathCommand>& commands);
  bool deleteNodes(const std::vector<std::string>& ids);
  std::vector<Node> cloneNodes(const std::vector<std::string>& ids) const;
  std::vector<Node> extractNodes(const std::vector<std::string>& ids);
  std::vector<std::string> appendClonedNodes(std::vector<Node> nodes, const BLPoint& offset);
  std::string groupNodes(const std::vector<std::string>& ids);
  std::vector<std::string> ungroupNodes(const std::vector<std::string>& ids);

  static BLPath commandsToPath(const std::vector<PathCommand>& commands);
  static std::vector<PathCommand> pathToCommands(const BLPath& path);
  static BLMatrix2D compose(const BLMatrix2D& parent, const BLMatrix2D& local);
  static bool invertMatrix(const BLMatrix2D& source, BLMatrix2D& inverse);
  static BLRect unionRect(const BLRect& a, const BLRect& b);
  static bool isIdentity(const BLMatrix2D& matrix);
  static std::string matrixToSvg(const BLMatrix2D& matrix);

 private:
  std::string hitTestNode(const Node& node, const BLMatrix2D& parentWorld, const BLPoint& scenePoint) const;
  void collectHitLeafIds(const Node& node,
                         const BLMatrix2D& parentWorld,
                         const BLPoint& scenePoint,
                         std::vector<std::string>& outIds) const;
  bool nodeBounds(const Node& node, const BLMatrix2D& parentWorld, BLRect& outBounds) const;
  void collectMarquee(const Node& node,
                      const BLMatrix2D& parentWorld,
                      const BLRect& marqueeRect,
                      std::vector<std::string>& outIds) const;
  void renderNode(BLContext& ctx, const Node& node) const;
  void renderPathNode(BLContext& ctx, const Node& node) const;
  void renderImageNode(BLContext& ctx, const Node& node) const;
  void setFillStyle(BLContext& ctx, const Style& style) const;
  void setStrokeStyle(BLContext& ctx, const Style& style) const;
  std::optional<BLPattern> patternFor(const Paint& paint) const;
  BLGradient gradientFor(const Paint& paint) const;
  bool pathHitTest(const Node& node, const BLPoint& scenePoint) const;
  bool imageHitTest(const Node& node, const BLPoint& scenePoint) const;
  bool extractNodesRecursive(Node& parent,
                             const std::vector<std::string>& ids,
                             std::vector<Node>& outNodes);
  bool containsNodeRecursive(const Node& node, const std::string& id) const;
  Node* findNodeRecursive(Node& node, const std::string& id);
  const Node* findNodeRecursive(const Node& node, const std::string& id) const;
  bool worldTransformRecursive(const Node& node,
                               const BLMatrix2D& parentWorld,
                               const std::string& id,
                               BLMatrix2D& outWorld,
                               BLMatrix2D& outParentWorld) const;
  void assignFreshIds(Node& node);
  void collectCloneRecursive(const Node& node,
                             const std::vector<std::string>& ids,
                             std::vector<Node>& outNodes) const;
  static void offsetNode(Node& node, const BLPoint& offset);

  Node root_;
  double viewBoxX_ = 0.0;
  double viewBoxY_ = 0.0;
  double width_ = 297.0;
  double height_ = 210.0;
  std::string sourcePath_;
  int nextNodeId_ = 1;
  mutable std::unordered_map<std::string, BLImage> patternImageCache_;
  mutable std::unordered_map<std::string, BLImage> externalImageCache_;
  std::unordered_map<std::string, GradientDef> gradients_;
  std::unordered_map<std::string, PatternDef> patterns_;
};

}  // namespace SvgEditor
