#pragma once

#include "GroupEdit.h"
#include "PointSelectionController.h"
#include "ShapeSelectionController.h"
#include "SvgSceneIO.h"
#include "UndoRedo.h"

#include "FileDialog.h"
#include "Layout.h"
#include "SceneRenderer.h"

#include <string>
#include <vector>

namespace SvgEditor {

class SvgEditorApp {
 public:
  enum class ToolMode {
    Select,
    PointEdit
  };

  bool renderFrame(Blend2DUI::SceneRenderer& renderer, double seconds);

 private:
  enum class PendingDialogAction {
    None,
    NewDocument,
    OpenDocument
  };

  enum class OverlayState {
    None,
    SaveFirstPrompt
  };

  enum class GridUnit {
    Millimetres,
    Inches
  };

  void ensureInitialized();
  RenderState currentRenderState(const BLRect& canvasRect) const;
  double gridMinorStepMm() const;
  double gridMajorStepMm() const;
  double gridSnapStepScene(const RenderState& renderState) const;
  bool handleViewport(Blend2DUI::SceneRenderer& renderer,
                      const BLRect& canvasRect,
                      const BLPoint& mouseScreen,
                      bool editorBlocked,
                      RenderState& renderState);
  void resetViewport();
  void renderToolbar(Blend2DUI::SceneRenderer& renderer, const BLRect& rect);
  void renderCanvas(Blend2DUI::SceneRenderer& renderer, const BLRect& rect);
  void renderGridOverlay(Blend2DUI::SceneRenderer& renderer, const RenderState& renderState) const;
  void renderDialogs(Blend2DUI::SceneRenderer& renderer, const BLRect& toolbarRect);
  void renderSettingsPopup(Blend2DUI::SceneRenderer& renderer, const BLRect& toolbarRect);
  void handleKeyboard(Blend2DUI::SceneRenderer& renderer);
  void requestDocumentReset(PendingDialogAction action);
  void performPendingAction();
  void startOpenDialog(bool merge);
  void startSaveDialog();
  bool saveTo(const std::string& path);
  void captureUndoState();
  EditorSnapshot snapshot() const;
  void restoreSnapshot(const EditorSnapshot& snapshot);

  SvgDocument document_;
  ShapeSelectionController shapeSelection_;
  PointSelectionController pointSelection_;
  UndoRedo undoRedo_;
  ToolMode toolMode_ = ToolMode::Select;
  bool initialized_ = false;
  bool showFileDialog_ = false;
  bool mergeOnAccept_ = false;
  Blend2DUI::UI_FileDialogMode fileDialogMode_ = Blend2DUI::UI_FileDialogMode::Open;
  std::string dialogSelectedPath_;
  std::string currentFilePath_;
  OverlayState overlayState_ = OverlayState::None;
  PendingDialogAction pendingAction_ = PendingDialogAction::None;
  bool saveThenContinue_ = false;
  std::vector<Node> clipboard_;
  double viewportZoom_ = 1.0;
  BLPoint viewportPan_{};
  bool viewportPanning_ = false;
  BLPoint viewportPanStartMouse_{};
  BLPoint viewportPanStartOffset_{};
  BLRect settingsPopupRect_{};
  bool settingsPopupRectInitialized_ = false;
  bool settingsPopupOpen_ = false;
  bool settingsPopupOpenedThisFrame_ = false;
  bool settingsPopupDragging_ = false;
  BLPoint settingsPopupDragOffset_{};
  bool gridVisible_ = false;
  GridUnit gridUnit_ = GridUnit::Millimetres;
  bool gridSnapEnabled_ = false;
  bool showAllBezierHandles_ = false;
};

}  // namespace SvgEditor
