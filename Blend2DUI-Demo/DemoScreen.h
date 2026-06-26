#pragma once

#include "Canvas3D.h"
#include "SceneRenderer.h"

#include <memory>
#include <string>
#include <vector>

namespace Blend2DUI {

class DemoScreen;

class DemoPanel {
 public:
  virtual ~DemoPanel() = default;

  virtual void render(DemoScreen& screen,
                      SceneRenderer& renderer,
                      const BLRect& rect,
                      double seconds,
                      double pulse) = 0;
};

class DemoScreen {
 public:
  DemoScreen();
  explicit DemoScreen(std::unique_ptr<DemoPanel> panel);

  bool renderFrame(SceneRenderer& renderer, double seconds);
  void setPanel(std::unique_ptr<DemoPanel> panel);

  void openFileDialog(UI_FileDialogMode mode,
                      std::vector<UI_FileTypeFilter> filters = {},
                      std::string defaultFileName = {});
  const std::string& selectedFilePath() const { return selectedFilePath_; }

 private:
  void renderMenu(SceneRenderer& renderer,
                  const UI_RectArea& menuArea,
                  bool& openedFileDialogThisFrame);
  void renderInputs(SceneRenderer& renderer, const UI_RectArea& inputArea);
  void renderSliders(SceneRenderer& renderer, const UI_RectArea& inputArea);
  void renderWidgetShowcase(SceneRenderer& renderer, const BLRect& rect);
  void renderFpsCounter(SceneRenderer& renderer, double width, double height);
  void updateFpsCounter(double seconds);
  void renderPanel(SceneRenderer& renderer,
                   double seconds,
                   double pulse,
                   double width,
                   double height,
                   double panelY);

  std::unique_ptr<DemoPanel> panel_;
  bool showFileDialog_ = false;
  UI_FileDialogMode fileDialogMode_ = UI_FileDialogMode::Open;
  std::vector<UI_FileTypeFilter> fileDialogFilters_;
  std::string fileDialogDefaultFileName_;
  std::string selectedFilePath_;
  std::string singleLineText_;
  std::string multiLineText_;
  double horizontalSliderValue_ = 64.0;
  double redHorizontalSliderValue_ = 28.0;
  double greenHorizontalSliderValue_ = 72.0;
  double verticalSliderValue_ = 0.35;
  bool tickBoxChecked_ = true;
  bool toggleEnabled_ = false;
  int contextMenuSelection_ = -1;
  double previewSplitRatio_ = 0.66;
  double previewDividerGrabOffset_ = 0.0;
  bool draggingPreviewDivider_ = false;
  double widgetShowcaseScroll_ = 0.0;
  double widgetShowcaseScrollbarDragOffset_ = 0.0;
  bool draggingWidgetShowcaseScrollbar_ = false;
  double lastFrameSeconds_ = -1.0;
  double smoothedFrameDelta_ = 1.0 / 60.0;
  double displayedFps_ = 60.0;
  Canvas3D canvas3D_;
};

}  // namespace Blend2DUI
