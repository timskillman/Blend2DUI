#include "SvgEditor/SvgEditorApp.h"

#include "Utility.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <string_view>

namespace SvgEditor {
namespace {

using Blend2DUI::UI_ButtonActionPressed;
using Blend2DUI::contains;

const Blend2DUI::UI_ButtonStyleDefinition kToolbarButtonStyle(
    "Gradients[#67E8F9,#22D3EE,#2563EB,#67E8F9], GradientAngle:45, "
    "GradientHover:Cycle, StrokeColour:#0891B2, StrokeWidth:1, "
    "Corner:14");
const Blend2DUI::UI_ButtonStyleDefinition kToolbarButtonActiveStyle(
    "Gradients[#EED322,#E9A50E,#D84E1D,#EED322], GradientAngle:45, "
    "GradientHover:Cycle, StrokeColour:#E0F2FE, StrokeWidth:1.2, "
    "Corner:14");
const Blend2DUI::UI_ButtonStyleDefinition kHandleButtonStyle(
    "Gradients[#67E8F9,#22D3EE,#2563EB], GradientAngle:45, "
    "GradientHover:Cycle, StrokeColour:#0EA5E9, StrokeWidth:1, "
    "Corner:8");
const Blend2DUI::UI_ButtonStyleDefinition kOverlayButtonStyle(
    "FillColour:#FFFFFF, HoverColour:#F4F7FB, PressedColour:#E7EEF7, "
    "StrokeColour:#CBD5E1, StrokeWidth:1, Corner:8, Font:DejaVuSans, FontSize:15, TextColour:#0F172A");
const Blend2DUI::UI_ButtonStyleDefinition kOverlayPrimaryButtonStyle(
    "FillColour:#1D4ED8, HoverColour:#2563EB, PressedColour:#1E40AF, "
    "StrokeColour:#1E3A8A, StrokeWidth:1, Corner:8, Font:DejaVuSans, FontSize:15, TextColour:#FFFFFF");
const Blend2DUI::UI_ButtonStyleDefinition kOverlayLabelStyle(
    "HasFill:false, HasStroke:false, Font:DejaVuSans, FontSize:16, TextColour:#0F172A");
const Blend2DUI::UI_ButtonStyleDefinition kOverlayHintStyle(
    "HasFill:false, HasStroke:false, Font:DejaVuSans, FontSize:13, TextColour:#475569");
const Blend2DUI::UI_ButtonStyleDefinition kOverlaySectionStyle(
    "HasFill:false, HasStroke:false, Font:DejaVuSans, FontSize:15, TextColour:#0F172A");
const Blend2DUI::UI_ButtonStyleDefinition kOverlayToggleStyle(
    "FillColour:#E2E8F0, HoverColour:#DBEAFE, PressedColour:#2563EB, "
    "StrokeColour:#94A3B8, StrokeWidth:1, Corner:999, Font:DejaVuSans, FontSize:14, TextColour:#0F172A");
const Blend2DUI::UI_ButtonStyleDefinition kOverlaySegmentStyle(
    "FillColour:#FFFFFF, HoverColour:#F4F7FB, PressedColour:#E7EEF7, "
    "StrokeColour:#CBD5E1, StrokeWidth:1, Corner:8, Font:DejaVuSans, FontSize:14, TextColour:#0F172A");
const Blend2DUI::UI_ButtonStyleDefinition kOverlaySegmentActiveStyle(
    "FillColour:#1D4ED8, HoverColour:#2563EB, PressedColour:#1E40AF, "
    "StrokeColour:#1E3A8A, StrokeWidth:1, Corner:8, Font:DejaVuSans, FontSize:14, TextColour:#FFFFFF");
const Blend2DUI::UI_RectStyleDefinition kToolbarRectStyle(
    "Padding:10, RectFill:#F8FAFC, RectStroke:#D9E2EC, RectStrokeWidth:1, RectCorner:12");
const Blend2DUI::UI_RectStyleDefinition kPopupRectStyle(
    "Padding:16, RectFill:#FFFFFF, RectStroke:#D9E2EC, RectStrokeWidth:1, RectCorner:12");
const Blend2DUI::UI_RectStyleDefinition kPopupContentRectStyle("Padding:0");

constexpr double kToolbarMargin = 22.0;
constexpr double kToolbarHeight = 78.0;
constexpr double kToolbarButtonSize = 56.0;
constexpr double kCanvasGap = 16.0;
constexpr double kViewportZoomStep = 1.12;
constexpr double kViewportMinZoom = 0.1;
constexpr double kViewportMaxZoom = 320.0;
constexpr int kToolbarCursorGap = 3;
constexpr int kToolbarButtonGap = 8;
constexpr int kToolbarButtonGapExtra = kToolbarButtonGap - kToolbarCursorGap;
constexpr int kToolbarSeparatorWidth = 12;
constexpr int kToolbarSeparatorAdvance = kToolbarSeparatorWidth - kToolbarCursorGap;
constexpr double kSettingsPopupWidth = 320.0;
constexpr double kSettingsPopupHeight = 252.0;
constexpr double kSettingsPopupHeaderHeight = 38.0;
constexpr double kA4LandscapeWidthMm = 297.0;
constexpr double kA4LandscapeHeightMm = 210.0;
constexpr double kInchInMm = 25.4;

const std::string iconPath = "assets/icons/";

bool iconButton(Blend2DUI::SceneRenderer& renderer,
                const std::string& id,
                std::string_view assetPath,
                bool active = false) {
  const auto& style = active ? kToolbarButtonActiveStyle : kToolbarButtonStyle;
  renderer.UI_CursorVerticalCenter();
  return renderer.UI_Button(id,
                            "56x56",
                            style,
                            Blend2DUI::UI_ButtonContent("", "", assetPath)) == UI_ButtonActionPressed;
}

void advanceToolbarButtonGap(Blend2DUI::SceneRenderer& renderer) {
  renderer.UI_CursorGap(kToolbarButtonGapExtra);
}

void toolbarSeparator(Blend2DUI::SceneRenderer& renderer) {
  renderer.UI_CursorOffset(-static_cast<double>(kToolbarCursorGap), 0.0);
  renderer.UI_CursorLine();
  renderer.UI_CursorGap(kToolbarSeparatorAdvance);
}

bool ctrlDown(const Blend2DUI::UI_TextInputKeyEvent& event) {
  return (event.mod & SDL_KMOD_CTRL) != 0;
}

bool shiftDown(const Blend2DUI::UI_TextInputKeyEvent& event) {
  return (event.mod & SDL_KMOD_SHIFT) != 0;
}

double snapValueToStep(double value, double step) {
  if (step <= 1.0e-6) return value;
  return std::round(value / step) * step;
}

BLRect clampPopupRect(const BLRect& rect, double windowWidth, double windowHeight) {
  const double clampedW = std::min(rect.w, std::max(120.0, windowWidth - 12.0));
  const double clampedH = std::min(rect.h, std::max(120.0, windowHeight - 12.0));
  const double minX = 6.0;
  const double minY = 6.0;
  const double maxX = std::max(minX, windowWidth - clampedW - 6.0);
  const double maxY = std::max(minY, windowHeight - clampedH - 6.0);
  return BLRect(std::clamp(rect.x, minX, maxX),
                std::clamp(rect.y, minY, maxY),
                clampedW,
                clampedH);
}

}  // namespace

bool SvgEditorApp::renderFrame(Blend2DUI::SceneRenderer& renderer, double seconds) {
  ensureInitialized();
  if (!renderer.beginFrame(seconds)) return false;
  settingsPopupOpenedThisFrame_ = false;

  const double width = static_cast<double>(renderer.width());
  const double height = static_cast<double>(renderer.height());
  if (settingsPopupRectInitialized_) {
    settingsPopupRect_ = clampPopupRect(settingsPopupRect_, width, height);
  }
  const BLRect toolbarRect(kToolbarMargin, 18.0, std::max(120.0, width - kToolbarMargin * 2.0), kToolbarHeight);
  const BLRect canvasRect(kToolbarMargin,
                          toolbarRect.y + toolbarRect.h + kCanvasGap,
                          std::max(120.0, width - kToolbarMargin * 2.0),
                          std::max(120.0, height - toolbarRect.y - toolbarRect.h - kCanvasGap - 18.0));

  handleKeyboard(renderer);
  renderCanvas(renderer, canvasRect);
  renderToolbar(renderer, toolbarRect);
  renderDialogs(renderer, toolbarRect);

  return renderer.endFrame();
}

void SvgEditorApp::ensureInitialized() {
  if (initialized_) return;
  document_.resetToA4Landscape();
  resetViewport();
  shapeSelection_.clear();
  pointSelection_.clear();
  undoRedo_.reset(snapshot());
  initialized_ = true;
}

RenderState SvgEditorApp::currentRenderState(const BLRect& canvasRect) const {
  return document_.createRenderState(canvasRect, viewportZoom_, viewportPan_);
}

double SvgEditorApp::gridMinorStepMm() const {
  return gridUnit_ == GridUnit::Millimetres ? 1.0 : (kInchInMm / 10.0);
}

double SvgEditorApp::gridMajorStepMm() const {
  return gridUnit_ == GridUnit::Millimetres ? 10.0 : kInchInMm;
}

double SvgEditorApp::gridSnapStepScene(const RenderState& renderState) const {
  const double paperScale = renderState.paperRect.w / kA4LandscapeWidthMm;
  if (paperScale <= 1.0e-6 || renderState.scale <= 1.0e-6) return 0.0;
  const double contentScale = renderState.scale / paperScale;
  if (contentScale <= 1.0e-6) return 0.0;
  return gridMinorStepMm() / contentScale;
}

bool SvgEditorApp::handleViewport(Blend2DUI::SceneRenderer& renderer,
                                  const BLRect& canvasRect,
                                  const BLPoint& mouseScreen,
                                  bool editorBlocked,
                                  RenderState& renderState) {
  const bool mouseInsideCanvas = contains(canvasRect, mouseScreen.x, mouseScreen.y);
  float mouseX = 0.0f;
  float mouseY = 0.0f;
  const SDL_MouseButtonFlags mouseButtons = SDL_GetMouseState(&mouseX, &mouseY);
  (void)mouseX;
  (void)mouseY;
  const bool middleDown = (mouseButtons & SDL_BUTTON_MASK(SDL_BUTTON_MIDDLE)) != 0;

  if (!editorBlocked && mouseInsideCanvas && std::abs(renderer.wheelY()) > 0.0) {
    const BLPoint anchorScene = renderState.screenToScene.map_point(mouseScreen);
    const double previousZoom = viewportZoom_;
    viewportZoom_ = std::clamp(viewportZoom_ * std::pow(kViewportZoomStep, renderer.wheelY()),
                               kViewportMinZoom,
                               kViewportMaxZoom);
    if (std::abs(viewportZoom_ - previousZoom) > 1.0e-6) {
      renderState = currentRenderState(canvasRect);
      const BLPoint anchorScreen = renderState.sceneToScreen.map_point(anchorScene);
      viewportPan_ = BLPoint(viewportPan_.x + mouseScreen.x - anchorScreen.x,
                             viewportPan_.y + mouseScreen.y - anchorScreen.y);
      renderState = currentRenderState(canvasRect);
    }
  }

  if (!middleDown) {
    viewportPanning_ = false;
    return false;
  }

  if (!viewportPanning_) {
    if (editorBlocked || !mouseInsideCanvas) return false;
    viewportPanning_ = true;
    viewportPanStartMouse_ = mouseScreen;
    viewportPanStartOffset_ = viewportPan_;
  }

  viewportPan_ = BLPoint(viewportPanStartOffset_.x + mouseScreen.x - viewportPanStartMouse_.x,
                         viewportPanStartOffset_.y + mouseScreen.y - viewportPanStartMouse_.y);
  renderState = currentRenderState(canvasRect);
  return true;
}

void SvgEditorApp::resetViewport() {
  viewportZoom_ = 1.0;
  viewportPan_ = BLPoint();
  viewportPanning_ = false;
  viewportPanStartMouse_ = BLPoint();
  viewportPanStartOffset_ = BLPoint();
}

void SvgEditorApp::renderToolbar(Blend2DUI::SceneRenderer& renderer, const BLRect& rect) {
  Blend2DUI::UI_RectArea area;
  area.SetRect(rect, kToolbarRectStyle);
  renderer.UI_CursorRect(area);
  
  if (iconButton(renderer, "toolbar.new", iconPath + "new.svg")) {
    requestDocumentReset(PendingDialogAction::NewDocument);
  }
  advanceToolbarButtonGap(renderer);
  if (iconButton(renderer, "toolbar.open", iconPath + "open-svg.svg")) {
    requestDocumentReset(PendingDialogAction::OpenDocument);
  }
  advanceToolbarButtonGap(renderer);
  if (iconButton(renderer, "toolbar.save", iconPath + "save.svg")) {
    startSaveDialog();
  }
  advanceToolbarButtonGap(renderer);
  if (iconButton(renderer, "toolbar.merge", iconPath + "folder.svg")) {
    startOpenDialog(true);
  }

  advanceToolbarButtonGap(renderer);
  toolbarSeparator(renderer);

  if (iconButton(renderer, "toolbar.select", iconPath + "path-edit.svg", toolMode_ == ToolMode::Select)) {
    toolMode_ = ToolMode::Select;
    pointSelection_.clear();
  }
  advanceToolbarButtonGap(renderer);
  if (iconButton(renderer, "toolbar.point", iconPath + "point-edit.svg", toolMode_ == ToolMode::PointEdit)) {
    toolMode_ = ToolMode::PointEdit;
  }

  advanceToolbarButtonGap(renderer);
  toolbarSeparator(renderer);

  if (iconButton(renderer, "toolbar.undo", iconPath + "undo.svg")) {
    if (auto restored = undoRedo_.undo(snapshot())) restoreSnapshot(*restored);
  }
  advanceToolbarButtonGap(renderer);
  if (iconButton(renderer, "toolbar.redo", iconPath + "redo.svg")) {
    if (auto restored = undoRedo_.redo(snapshot())) restoreSnapshot(*restored);
  }
  advanceToolbarButtonGap(renderer);
  if (iconButton(renderer, "toolbar.group", iconPath + "group.svg")) {
    if (GroupEdit::groupSelection(document_, shapeSelection_.tool())) {
      pointSelection_.clear();
      captureUndoState();
    }
  }
  advanceToolbarButtonGap(renderer);
  if (iconButton(renderer, "toolbar.ungroup", iconPath + "ungroup.svg")) {
    if (GroupEdit::ungroupSelection(document_, shapeSelection_.tool())) {
      pointSelection_.clear();
      captureUndoState();
    }
  }
  advanceToolbarButtonGap(renderer);
  toolbarSeparator(renderer);
  if (iconButton(renderer, "toolbar.settings", iconPath + "line-style.svg", settingsPopupOpen_)) {
    settingsPopupOpen_ = !settingsPopupOpen_;
    settingsPopupOpenedThisFrame_ = settingsPopupOpen_;
    settingsPopupDragging_ = false;
    if (settingsPopupOpen_ && !settingsPopupRectInitialized_) {
      settingsPopupRect_ = clampPopupRect(BLRect(rect.x + rect.w - kSettingsPopupWidth,
                                                 rect.y + rect.h + 8.0,
                                                 kSettingsPopupWidth,
                                                 kSettingsPopupHeight),
                                          static_cast<double>(renderer.width()),
                                          static_cast<double>(renderer.height()));
      settingsPopupRectInitialized_ = true;
    }
  }
}

void SvgEditorApp::renderCanvas(Blend2DUI::SceneRenderer& renderer, const BLRect& rect) {
  const BLPoint mouseScreen(renderer.mouseX(), renderer.mouseY());
  const bool mouseInsideCanvas = contains(rect, mouseScreen.x, mouseScreen.y);
  const bool popupConsumesPointer =
      settingsPopupOpen_ &&
      settingsPopupRectInitialized_ &&
      contains(settingsPopupRect_, mouseScreen.x, mouseScreen.y);
  const bool editorBlocked =
      showFileDialog_ || overlayState_ != OverlayState::None || settingsPopupDragging_ || popupConsumesPointer;
  RenderState renderState = currentRenderState(rect);
  const bool viewportPanning = handleViewport(renderer, rect, mouseScreen, editorBlocked, renderState);
  const BLPoint mouseScene = renderState.screenToScene.map_point(mouseScreen);

  document_.render(renderer.context(), rect, renderState);
  renderGridOverlay(renderer, renderState);

  if (!editorBlocked && !viewportPanning) {
    if (toolMode_ == ToolMode::Select) {
      if (mouseInsideCanvas || shapeSelection_.interactionActive()) {
        if (const ShapeSelectionResult result =
                shapeSelection_.handleInteraction(renderer,
                                                 mouseScreen,
                                                 mouseScene,
                                                 renderState,
                                                 document_,
                                                 clipboard_,
                                                 gridSnapEnabled_,
                                                 gridSnapStepScene(renderState));
            result.captureUndo) {
          pointSelection_.clear();
          captureUndoState();
        }
      }
    } else {
      if (mouseInsideCanvas || pointSelection_.interactionActive()) {
        if (const PointSelectionResult result =
                pointSelection_.handleInteraction(renderer,
                                                 mouseScreen,
                                                 mouseScene,
                                                 renderState,
                                                 document_,
                                                 shapeSelection_.tool(),
                                                 showAllBezierHandles_,
                                                 gridSnapEnabled_,
                                                 gridSnapStepScene(renderState));
            result.captureUndo) {
          captureUndoState();
        }
      }
    }
  }

  BLContext& ctx = renderer.context();
  BLContextCookie overlayClipCookie;
  ctx.save(overlayClipCookie);
  ctx.clip_to_rect(rect);
  const std::string handleIconPath = iconPath + "fit.svg";
  shapeSelection_.renderOverlay(renderer, document_, renderState, kHandleButtonStyle, handleIconPath);
  if (toolMode_ == ToolMode::PointEdit) {
    pointSelection_.renderOverlay(renderer, document_, shapeSelection_.tool(), renderState, showAllBezierHandles_);
  }
  ctx.restore(overlayClipCookie);
}

void SvgEditorApp::renderGridOverlay(Blend2DUI::SceneRenderer& renderer, const RenderState& renderState) const {
  if (!gridVisible_) return;

  const double paperScale = renderState.paperRect.w / kA4LandscapeWidthMm;
  if (paperScale <= 1.0e-6) return;

  BLContext& ctx = renderer.context();
  BLContextCookie cookie;
  ctx.save(cookie);
  ctx.clip_to_rect(renderState.paperRect);

  const double minorStepMm = gridMinorStepMm();
  const int majorEvery = std::max(1, static_cast<int>(std::round(gridMajorStepMm() / minorStepMm)));

  for (int index = 0;; ++index) {
    const double xMm = minorStepMm * static_cast<double>(index);
    if (xMm > kA4LandscapeWidthMm + 1.0e-6) break;
    const double x = renderState.paperRect.x + xMm * paperScale;
    const bool major = (index % majorEvery) == 0;
    ctx.set_stroke_style(BLRgba32(major ? 0x3B64748Bu : 0x1864748Bu));
    ctx.set_stroke_width(major ? 1.0 : 0.6);
    ctx.stroke_line(x, renderState.paperRect.y, x, renderState.paperRect.y + renderState.paperRect.h);
  }

  for (int index = 0;; ++index) {
    const double yMm = minorStepMm * static_cast<double>(index);
    if (yMm > kA4LandscapeHeightMm + 1.0e-6) break;
    const double y = renderState.paperRect.y + yMm * paperScale;
    const bool major = (index % majorEvery) == 0;
    ctx.set_stroke_style(BLRgba32(major ? 0x3B64748Bu : 0x1864748Bu));
    ctx.set_stroke_width(major ? 1.0 : 0.6);
    ctx.stroke_line(renderState.paperRect.x, y, renderState.paperRect.x + renderState.paperRect.w, y);
  }

  ctx.restore(cookie);
}

void SvgEditorApp::renderSettingsPopup(Blend2DUI::SceneRenderer& renderer, const BLRect& toolbarRect) {
  if (!settingsPopupRectInitialized_) {
    settingsPopupRect_ = clampPopupRect(BLRect(toolbarRect.x + toolbarRect.w - kSettingsPopupWidth,
                                               toolbarRect.y + toolbarRect.h + 8.0,
                                               kSettingsPopupWidth,
                                               kSettingsPopupHeight),
                                        static_cast<double>(renderer.width()),
                                        static_cast<double>(renderer.height()));
    settingsPopupRectInitialized_ = true;
  }

  const BLPoint mouse(renderer.mouseX(), renderer.mouseY());
  const BLRect closeRect(settingsPopupRect_.x + settingsPopupRect_.w - 34.0,
                         settingsPopupRect_.y + 8.0,
                         24.0,
                         24.0);
  const BLRect headerRect(settingsPopupRect_.x + 10.0,
                          settingsPopupRect_.y + 8.0,
                          settingsPopupRect_.w - 52.0,
                          kSettingsPopupHeaderHeight - 8.0);

  if (renderer.mousePressed() &&
      contains(headerRect, mouse.x, mouse.y) &&
      !contains(closeRect, mouse.x, mouse.y)) {
    settingsPopupDragging_ = true;
    settingsPopupDragOffset_ = BLPoint(mouse.x - settingsPopupRect_.x, mouse.y - settingsPopupRect_.y);
  }
  if (settingsPopupDragging_ && renderer.mouseDown()) {
    settingsPopupRect_ = clampPopupRect(BLRect(mouse.x - settingsPopupDragOffset_.x,
                                               mouse.y - settingsPopupDragOffset_.y,
                                               settingsPopupRect_.w,
                                               settingsPopupRect_.h),
                                        static_cast<double>(renderer.width()),
                                        static_cast<double>(renderer.height()));
  }
  if (renderer.mouseReleased()) settingsPopupDragging_ = false;

  Blend2DUI::UI_RectArea area;
  area.SetRect(settingsPopupRect_, kPopupRectStyle);
  renderer.UI_CursorRect(area);

  renderer.UI_Label("settings.title",
                    BLRect(settingsPopupRect_.x + 16.0,
                           settingsPopupRect_.y + 8.0,
                           settingsPopupRect_.w - 60.0,
                           24.0),
                    kOverlaySectionStyle,
                    Blend2DUI::UI_ButtonContent("Settings"));
  if (renderer.UI_Button("settings.close",
                         closeRect,
                         kOverlayButtonStyle,
                         Blend2DUI::UI_ButtonContent("x")) == UI_ButtonActionPressed) {
    settingsPopupOpen_ = false;
    settingsPopupDragging_ = false;
    return;
  }

  Blend2DUI::UI_RectArea contentArea;
  contentArea.SetRect(BLRect(settingsPopupRect_.x + 16.0,
                             settingsPopupRect_.y + kSettingsPopupHeaderHeight,
                             settingsPopupRect_.w - 32.0,
                             settingsPopupRect_.h - kSettingsPopupHeaderHeight - 14.0),
                      kPopupContentRectStyle);
  renderer.UI_CursorRect(contentArea);

  renderer.UI_Label("settings.grid.section", "100%x20", kOverlaySectionStyle, Blend2DUI::UI_ButtonContent("Grid"));
  renderer.UI_CursorNext();
  renderer.UI_Toggle("settings.grid.visible",
                     "100%x30",
                     gridVisible_,
                     kOverlayToggleStyle,
                     Blend2DUI::UI_ButtonContent("Show grid"));
  renderer.UI_CursorNext();

  renderer.UI_Label("settings.grid.units", "48x30", kOverlayLabelStyle, Blend2DUI::UI_ButtonContent("Units"));
  if (renderer.UI_Button("settings.grid.unit.mm",
                         "52x30",
                         gridUnit_ == GridUnit::Millimetres ? kOverlaySegmentActiveStyle : kOverlaySegmentStyle,
                         Blend2DUI::UI_ButtonContent("mm")) == UI_ButtonActionPressed) {
    gridUnit_ = GridUnit::Millimetres;
  }
  renderer.UI_CursorGap(8);
  if (renderer.UI_Button("settings.grid.unit.in",
                         "52x30",
                         gridUnit_ == GridUnit::Inches ? kOverlaySegmentActiveStyle : kOverlaySegmentStyle,
                         Blend2DUI::UI_ButtonContent("in")) == UI_ButtonActionPressed) {
    gridUnit_ = GridUnit::Inches;
  }
  renderer.UI_CursorGap(3);
  renderer.UI_CursorNext();
  renderer.UI_Label("settings.grid.note",
                    "100%x18",
                    kOverlayHintStyle,
                    Blend2DUI::UI_ButtonContent(gridUnit_ == GridUnit::Millimetres
                                                    ? "1 mm steps, 10 mm majors"
                                                    : "0.1 in steps, 1 in majors"));
  renderer.UI_CursorNext();
  renderer.UI_Toggle("settings.grid.snap",
                     "100%x30",
                     gridSnapEnabled_,
                     kOverlayToggleStyle,
                     Blend2DUI::UI_ButtonContent("Snap to grid"));
  renderer.UI_CursorNext();
  renderer.UI_Label("settings.point.section", "100%x20", kOverlaySectionStyle, Blend2DUI::UI_ButtonContent("Point edit"));
  renderer.UI_CursorNext();
  renderer.UI_Toggle("settings.point.all-handles",
                     "100%x30",
                     showAllBezierHandles_,
                     kOverlayToggleStyle,
                     Blend2DUI::UI_ButtonContent("All bezier handles"));
  renderer.UI_CursorNext();
  renderer.UI_Label("settings.point.note",
                    "100%x18",
                    kOverlayHintStyle,
                    Blend2DUI::UI_ButtonContent("Off: selected points only"));
}

void SvgEditorApp::renderDialogs(Blend2DUI::SceneRenderer& renderer, const BLRect& toolbarRect) {
  if (showFileDialog_) {
    Blend2DUI::UI_FileDialogOptions options;
    options.mode = fileDialogMode_;
    options.title = fileDialogMode_ == Blend2DUI::UI_FileDialogMode::Save ? "Save SVG Scene" : (mergeOnAccept_ ? "Merge SVG Scene" : "Open SVG Scene");
    options.filters = {{"SVG files", "*.svg"}};
    options.defaultFileName = currentFilePath_.empty() ? "scene.svg" : std::filesystem::path(currentFilePath_).filename().string();
    const Blend2DUI::UI_FileDialogResult result = Blend2DUI::renderFileDialog(renderer,
                                                                               "svg-editor-file-dialog",
                                                                               showFileDialog_,
                                                                               options,
                                                                               dialogSelectedPath_);
    if (result == Blend2DUI::UI_FileDialogResult::Accepted) {
      if (fileDialogMode_ == Blend2DUI::UI_FileDialogMode::Save) {
        if (saveTo(dialogSelectedPath_) && saveThenContinue_) {
          saveThenContinue_ = false;
          performPendingAction();
        }
      } else {
        SvgDocument loaded;
        if (loadSvgDocument(dialogSelectedPath_, loaded)) {
          if (mergeOnAccept_) {
            const std::vector<std::string> mergedIds = document_.mergeFrom(std::move(loaded));
            shapeSelection_.setSelection(mergedIds, document_);
            pointSelection_.clear();
            captureUndoState();
          } else {
            document_ = std::move(loaded);
            currentFilePath_ = dialogSelectedPath_;
            resetViewport();
            shapeSelection_.clear();
            pointSelection_.clear();
            undoRedo_.reset(snapshot());
          }
        }
      }
      mergeOnAccept_ = false;
    } else if (result == Blend2DUI::UI_FileDialogResult::Cancelled) {
      mergeOnAccept_ = false;
      if (saveThenContinue_) {
        saveThenContinue_ = false;
        pendingAction_ = PendingDialogAction::None;
      }
    }
  }

  if (!showFileDialog_ && overlayState_ == OverlayState::None && settingsPopupOpen_) {
    renderSettingsPopup(renderer, toolbarRect);
  }

  if (overlayState_ == OverlayState::SaveFirstPrompt) {
    BLContext& ctx = renderer.context();
    const double width = static_cast<double>(renderer.width());
    const double height = static_cast<double>(renderer.height());
    const BLRect dialogRect((width - 360.0) * 0.5, (height - 180.0) * 0.5, 360.0, 180.0);
    ctx.set_fill_style(BLRgba32(0x660F172Au));
    ctx.fill_rect(BLRect(0.0, 0.0, width, height));
    ctx.set_fill_style(BLRgba32(0xFFFFFFFFu));
    ctx.fill_round_rect(BLRoundRect(dialogRect.x, dialogRect.y, dialogRect.w, dialogRect.h, 12.0));
    ctx.set_stroke_style(BLRgba32(0xFFD0D7E2u));
    ctx.set_stroke_width(1.0);
    ctx.stroke_round_rect(BLRoundRect(dialogRect.x + 0.5, dialogRect.y + 0.5, dialogRect.w - 1.0, dialogRect.h - 1.0, 12.0));

    renderer.UI_Label("save-first-title",
                      BLRect(dialogRect.x + 24.0, dialogRect.y + 24.0, dialogRect.w - 48.0, 28.0),
                      kOverlayLabelStyle,
                      Blend2DUI::UI_ButtonContent("Save changes first?"));
    renderer.UI_Label("save-first-body",
                      BLRect(dialogRect.x + 24.0, dialogRect.y + 62.0, dialogRect.w - 48.0, 24.0),
                      kOverlayLabelStyle,
                      Blend2DUI::UI_ButtonContent("The current scene has unsaved edits."));

    const BLRect saveRect(dialogRect.x + 24.0, dialogRect.y + dialogRect.h - 52.0, 96.0, 30.0);
    const BLRect discardRect(saveRect.x + 108.0, saveRect.y, 104.0, 30.0);
    const BLRect cancelRect(discardRect.x + 116.0, saveRect.y, 92.0, 30.0);
    if (renderer.UI_Button("save-first-save", saveRect, kOverlayPrimaryButtonStyle, Blend2DUI::UI_ButtonContent("Save")) == UI_ButtonActionPressed) {
      overlayState_ = OverlayState::None;
      saveThenContinue_ = true;
      startSaveDialog();
    }
    if (renderer.UI_Button("save-first-discard", discardRect, kOverlayButtonStyle, Blend2DUI::UI_ButtonContent("Don't Save")) == UI_ButtonActionPressed) {
      overlayState_ = OverlayState::None;
      performPendingAction();
    }
    if (renderer.UI_Button("save-first-cancel", cancelRect, kOverlayButtonStyle, Blend2DUI::UI_ButtonContent("Cancel")) == UI_ButtonActionPressed) {
      overlayState_ = OverlayState::None;
      pendingAction_ = PendingDialogAction::None;
      saveThenContinue_ = false;
    }
  }
}

void SvgEditorApp::handleKeyboard(Blend2DUI::SceneRenderer& renderer) {
  if (showFileDialog_ || overlayState_ != OverlayState::None) return;
  for (const Blend2DUI::UI_TextInputKeyEvent& event : renderer.keyEvents()) {
    if (event.repeat) continue;

    if (event.key == SDLK_DELETE && toolMode_ == ToolMode::PointEdit &&
        pointSelection_.deleteSelected(document_, shapeSelection_.tool())) {
      captureUndoState();
      continue;
    }

    if (event.key == SDLK_DELETE && !shapeSelection_.empty()) {
      if (document_.deleteNodes(shapeSelection_.ids())) {
        shapeSelection_.clear();
        pointSelection_.clear();
        captureUndoState();
      }
      continue;
    }

    if (!ctrlDown(event)) continue;

    if (event.key == SDLK_G && shiftDown(event)) {
      if (GroupEdit::ungroupSelection(document_, shapeSelection_.tool())) {
        pointSelection_.clear();
        captureUndoState();
      }
    } else if (event.key == SDLK_G) {
      if (GroupEdit::groupSelection(document_, shapeSelection_.tool())) {
        pointSelection_.clear();
        captureUndoState();
      }
    } else if (event.key == SDLK_U) {
      if (GroupEdit::ungroupSelection(document_, shapeSelection_.tool())) {
        pointSelection_.clear();
        captureUndoState();
      }
    } else if (event.key == SDLK_C && !shapeSelection_.empty()) {
      clipboard_ = document_.cloneNodes(shapeSelection_.ids());
    } else if (event.key == SDLK_V && !clipboard_.empty()) {
      const std::vector<std::string> ids = document_.appendClonedNodes(clipboard_, BLPoint(12.0, 12.0));
      shapeSelection_.setSelection(ids, document_);
      pointSelection_.clear();
      captureUndoState();
    } else if (event.key == SDLK_Z) {
      if (auto restored = undoRedo_.undo(snapshot())) restoreSnapshot(*restored);
    } else if (event.key == SDLK_Y) {
      if (auto restored = undoRedo_.redo(snapshot())) restoreSnapshot(*restored);
    }
  }
}

void SvgEditorApp::requestDocumentReset(PendingDialogAction action) {
  settingsPopupOpen_ = false;
  pendingAction_ = action;
  if (undoRedo_.dirty()) overlayState_ = OverlayState::SaveFirstPrompt;
  else performPendingAction();
}

void SvgEditorApp::performPendingAction() {
  if (pendingAction_ == PendingDialogAction::NewDocument) {
    document_.resetToA4Landscape();
    resetViewport();
    currentFilePath_.clear();
    shapeSelection_.clear();
    pointSelection_.clear();
    toolMode_ = ToolMode::Select;
    undoRedo_.reset(snapshot());
  } else if (pendingAction_ == PendingDialogAction::OpenDocument) {
    startOpenDialog(false);
  }
  pendingAction_ = PendingDialogAction::None;
}

void SvgEditorApp::startOpenDialog(bool merge) {
  settingsPopupOpen_ = false;
  showFileDialog_ = true;
  mergeOnAccept_ = merge;
  fileDialogMode_ = Blend2DUI::UI_FileDialogMode::Open;
}

void SvgEditorApp::startSaveDialog() {
  settingsPopupOpen_ = false;
  showFileDialog_ = true;
  mergeOnAccept_ = false;
  fileDialogMode_ = Blend2DUI::UI_FileDialogMode::Save;
}

bool SvgEditorApp::saveTo(const std::string& path) {
  if (!saveSvgDocument(document_, path)) return false;
  currentFilePath_ = path;
  document_.setSourcePath(path);
  undoRedo_.replaceCurrent(snapshot());
  undoRedo_.markSaved();
  return true;
}

void SvgEditorApp::captureUndoState() {
  shapeSelection_.refreshBounds(document_);
  undoRedo_.capture(snapshot());
}

EditorSnapshot SvgEditorApp::snapshot() const {
  return EditorSnapshot{document_, shapeSelection_.ids(), currentFilePath_};
}

void SvgEditorApp::restoreSnapshot(const EditorSnapshot& state) {
  document_ = state.document;
  currentFilePath_ = state.filePath;
  shapeSelection_.setSelection(state.selectionIds, document_);
  pointSelection_.clear();
  toolMode_ = ToolMode::Select;
}

}  // namespace SvgEditor
