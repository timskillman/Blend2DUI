#pragma once

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
  void renderSliders(SceneRenderer& renderer, double width);
  void renderPanel(SceneRenderer& renderer,
                   double seconds,
                   double pulse,
                   double width,
                   double height);

  std::unique_ptr<DemoPanel> panel_;
  bool showFileDialog_ = false;
  UI_FileDialogMode fileDialogMode_ = UI_FileDialogMode::Open;
  std::vector<UI_FileTypeFilter> fileDialogFilters_;
  std::string fileDialogDefaultFileName_;
  std::string selectedFilePath_;
  std::string singleLineText_;
  std::string multiLineText_;
  double horizontalSliderValue_ = 64.0;
  double verticalSliderValue_ = 0.35;
};

}  // namespace Blend2DUI
