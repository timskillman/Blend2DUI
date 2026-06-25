#include "DemoScreen.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <memory>
#include <utility>

namespace {

struct DemoButtonStyles {
  Blend2DUI::UI_ButtonStyleDefinition select{
      "FillColour:#2563EB, HoverColour:#3B82F6, PressedColour:#1D4ED8, "
      "StrokeColour:#1E40AF, StrokeWidth:1, Corner:10, Font:DejaVuSans, FontSize:32"};
  Blend2DUI::UI_ButtonStyleDefinition gradient{
      "Gradients[#E0D030,#FFFFFF,#F97316,#E06010,#FFFFFF,#E0D030], GradientAngle:45, HoverColour:#808080, "
      "GradientHover:cycle, StrokeColour:#FF172A, StrokeWidth:5, Corner:18, FontSize:14"};
  Blend2DUI::UI_ButtonStyleDefinition image{
      "Layout:Left, FillColour:#FFFFFF, HoverColour:#EEF2FF, StrokeColour:#0FD5E1, "
      "StrokeWidth:2, TextColour:#0F172A, Corner:6"};
  Blend2DUI::UI_ButtonStyleDefinition input{
      "FillColour:#FFFFFF, HoverColour:#F8FAFC, PressedColour:#2563EB, StrokeColour:#CBD5E1, "
      "StrokeWidth:1, TextColour:#0F172A, Corner:12, Font:DejaVuSans, FontSize:16"};
  Blend2DUI::UI_ButtonStyleDefinition neutral{
      "FillColour:#F8FAFC, HoverColour:#E0F2FE, PressedColour:#BAE6FD, StrokeColour:#CBD5E1, "
      "StrokeWidth:1, TextColour:#0F172A, Corner:8, Font:DejaVuSans, FontSize:14"};
  Blend2DUI::UI_ButtonStyleDefinition slider{
      "FillColour:#FFFFFF, HoverColour:#E0F2FE, PressedColour:#38BDF8, StrokeColour:#94A3B8, "
      "StrokeWidth:1, TextColour:#0F172A, Corner:8, Font:DejaVuSans, FontSize:14"};
  Blend2DUI::UI_RectStyleDefinition menuRect{
      "Padding:16, RectFill:#FFFFFF, RectStroke:#D0D7E2, RectStrokeWidth:1, RectCorner:8"};
  Blend2DUI::UI_RectStyleDefinition inputRect{"Padding:0"};
  Blend2DUI::UI_TextInputOptions singleLineOptions{
      "Mode:SingleLine, MaxLength:256, Placeholder:'Single line text'"};
  Blend2DUI::UI_TextInputOptions multiLineOptions{
      "Mode:MultiLine, Resizable:true, Placeholder:'Multi-line text'"};
  Blend2DUI::UI_SliderOptions horizontalSliderOptions{
      "Heading:'Horizontal slider', Min:0, Max:100, Default:64, Step:1, Integer:true, Thumb:Circle"};
  Blend2DUI::UI_SliderOptions verticalSliderOptions{
      "Orientation:Vertical, Heading:'Vertical', Min:0, Max:1, Default:0.35, Step:0.05, Thumb:Diamond"};
};

const DemoButtonStyles kDemoStyles;
const std::vector<Blend2DUI::UI_FileTypeFilter> kDemoFileFilters = {
    {"All files", "*.*"},
    {"PNG images", "*.png"},
    {"SVG files", "*.svg"},
    {"Text files", "*.txt"},
};

class DemoProfileScope {
 public:
  DemoProfileScope(Blend2DUI::SceneRenderer& renderer, std::string name)
      : renderer_(renderer), name_(std::move(name)), active_(renderer_.profilingEnabled()) {
    if (active_) start_ = std::chrono::steady_clock::now();
  }

  ~DemoProfileScope() {
    if (!active_) return;
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start_).count();
    renderer_.profileSection(name_, ms);
  }

 private:
  Blend2DUI::SceneRenderer& renderer_;
  std::string name_;
  bool active_ = false;
  std::chrono::steady_clock::time_point start_;
};

class DefaultDemoPanel final : public Blend2DUI::DemoPanel {
 public:
  void render(Blend2DUI::DemoScreen& screen,
              Blend2DUI::SceneRenderer& renderer,
              const BLRect& rect,
              double,
              double pulse) override {
    BLContext& ctx = renderer.context();
    ctx.set_fill_style(BLRgba32(0xFF111827u));
    ctx.fill_round_rect(BLRoundRect(rect.x, rect.y, rect.w, rect.h, 6));

    BLGradient orb(BLRadialGradientValues(rect.x + rect.w * (0.1 + 0.3 * pulse),
                                          rect.y + rect.h * 0.5,
                                          rect.x + rect.w * 0.25,
                                          rect.y + rect.h * 0.35,
                                          std::min(rect.w, rect.h) * 0.45));
    orb.add_stop(0.0, BLRgba32(0xFFFFF7ADu));
    orb.add_stop(0.45, BLRgba32(0xFF38BDF8u));
    orb.add_stop(1.0, BLRgba32(0x00111827u));
    ctx.set_fill_style(orb);
    ctx.fill_round_rect(BLRoundRect(rect.x, rect.y, rect.w, rect.h, 6));

    if (!screen.selectedFilePath().empty()) {
      ctx.set_fill_style(BLRgba32(0xF0FFFFFFu));
      ctx.fill_round_rect(BLRoundRect(rect.x + 18.0,
                                      rect.y + rect.h - 54.0,
                                      std::min(560.0, rect.w - 36.0),
                                      34.0,
                                      6.0));
    }
  }
};

std::unique_ptr<Blend2DUI::DemoPanel> makeDefaultPanel() {
  return std::make_unique<DefaultDemoPanel>();
}

}  // namespace

namespace Blend2DUI {

DemoScreen::DemoScreen()
    : DemoScreen(makeDefaultPanel()) {}

DemoScreen::DemoScreen(std::unique_ptr<DemoPanel> panel)
    : panel_(panel ? std::move(panel) : makeDefaultPanel()),
      singleLineText_("Single line input - paste or type UTF-8 text"),
      multiLineText_("Multi-line text area with wrapping.\n"
                     "Try Japanese: こんにちは世界\n"
                     "Drag to select, Ctrl+C/Ctrl+V to copy and paste, arrows to move the caret.") {}

void DemoScreen::setPanel(std::unique_ptr<DemoPanel> panel) {
  panel_ = panel ? std::move(panel) : makeDefaultPanel();
}

void DemoScreen::openFileDialog(UI_FileDialogMode mode,
                                std::vector<UI_FileTypeFilter> filters,
                                std::string defaultFileName) {
  fileDialogMode_ = mode;
  fileDialogFilters_ = std::move(filters);
  fileDialogDefaultFileName_ = std::move(defaultFileName);
  showFileDialog_ = true;
}

bool DemoScreen::renderFrame(SceneRenderer& renderer, double seconds) {
  DemoProfileScope renderProfile(renderer, "demo.renderFrame");
  if (!renderer.beginFrame(seconds)) return false;

  bool openedFileDialogThisFrame = false;
  const double width = static_cast<double>(renderer.width());
  const double height = static_cast<double>(renderer.height());
  const double pulse = 0.5 + 0.5 * std::sin(seconds * 2.2);

  UI_RectArea menuArea;
  menuArea.SetRect(BLRect(28, 28, std::max(80.0, width - 56.0), 72), kDemoStyles.menuRect);
  UI_RectArea inputArea;
  inputArea.SetRect(BLRect(44, 116, std::max(260.0, width - 88.0), 362.0), kDemoStyles.inputRect);

  renderMenu(renderer, menuArea, openedFileDialogThisFrame);
  renderInputs(renderer, inputArea);
  renderSliders(renderer, width);
  renderPanel(renderer, seconds, pulse, width, height);

  UI_FileDialogOptions dialogOptions;
  dialogOptions.mode = fileDialogMode_;
  dialogOptions.title = fileDialogMode_ == UI_FileDialogMode::Save ? "Save Blend2D Output" : "Load File";
  dialogOptions.filters = fileDialogFilters_.empty() ? kDemoFileFilters : fileDialogFilters_;
  dialogOptions.defaultFileName = fileDialogDefaultFileName_;
  const UI_FileDialogResult fileDialogResult = Blend2DUI::renderFileDialog(renderer,
                                                                           "demo-file-dialog",
                                                                           showFileDialog_,
                                                                           openedFileDialogThisFrame,
                                                                           dialogOptions,
                                                                           selectedFilePath_);
  if (fileDialogResult == UI_FileDialogResult::Accepted) {
    std::cout << (fileDialogMode_ == UI_FileDialogMode::Save ? "Save path: " : "Load path: ") << selectedFilePath_ << "\n";
  }

  return renderer.endFrame();
}

void DemoScreen::renderMenu(SceneRenderer& renderer,
                            const UI_RectArea& menuArea,
                            bool& openedFileDialogThisFrame) {
  DemoProfileScope sectionProfile(renderer, "demo.menu");

  renderer.UI_CursorRect(menuArea);
  renderer.UI_CursorLeft(14);

  if (renderer.UI_Button("1", "132x40", kDemoStyles.select, UI_ButtonContent("Select", "Select a shape")) ==
      UI_ButtonActionPressed) {
    std::cout << "Select button pressed\n";
  }
  if (renderer.UI_Button("2", "170x40", kDemoStyles.gradient, UI_ButtonContent("Gradient", "Blend2D gradient button")) ==
      UI_ButtonActionPressed) {
    std::cout << "Gradient button pressed\n";
  }
  if (renderer.UI_Button("3", "80x40", kDemoStyles.image, UI_ButtonContent("", "SVG icon button", "assets/add-folder-blue.svg")) ==
      UI_ButtonActionPressed) {
    std::cout << "Image button pressed\n";
  }
  if (renderer.UI_Button("4", "104x40", kDemoStyles.neutral, UI_ButtonContent("Load...")) == UI_ButtonActionPressed) {
    openFileDialog(UI_FileDialogMode::Open, kDemoFileFilters);
    openedFileDialogThisFrame = true;
  }
  if (renderer.UI_Button("5", "104x40", kDemoStyles.neutral, UI_ButtonContent("Save...")) == UI_ButtonActionPressed) {
    openFileDialog(UI_FileDialogMode::Save, kDemoFileFilters, "blend2d-output.png");
    openedFileDialogThisFrame = true;
  }
}

void DemoScreen::renderInputs(SceneRenderer& renderer, const UI_RectArea& inputArea) {
  renderer.UI_CursorRect(inputArea);
  renderer.UI_CursorTop(12);

  DemoProfileScope sectionProfile(renderer, "demo.inputs");
  renderer.UI_TextInput("single-line-demo", "100%x40", kDemoStyles.singleLineOptions, singleLineText_, kDemoStyles.input);
  renderer.UI_TextInput("multi-line-demo", "100%x128", kDemoStyles.multiLineOptions, multiLineText_, kDemoStyles.input);
}

void DemoScreen::renderSliders(SceneRenderer& renderer, double width) {
  DemoProfileScope sliderProfile(renderer, "demo.sliders");
  renderer.UI_CursorGap(2);
  renderer.UI_Slider("horizontal-slider-demo",
                     "90%x58",
                     kDemoStyles.horizontalSliderOptions,
                     horizontalSliderValue_,
                     kDemoStyles.slider);

  renderer.UI_CursorSave("after-horizontal-slider");
  renderer.UI_CursorUse("after-horizontal-slider");
  renderer.UI_CursorOffset(std::max(0.0, width - 154.0), -70.0);
  renderer.UI_Slider("vertical-slider-demo",
                     "66x168",
                     kDemoStyles.verticalSliderOptions,
                     verticalSliderValue_,
                     kDemoStyles.slider);
}

void DemoScreen::renderPanel(SceneRenderer& renderer,
                             double seconds,
                             double pulse,
                             double width,
                             double height) {
  DemoProfileScope sectionProfile(renderer, "demo.panel");

  BLContext& ctx = renderer.context();
  const double panelY = 498.0;
  const double panelH = std::max(120.0, height - panelY - 32.0);
  const BLRect panelRect(28, panelY, std::max(80.0, width - 56.0), panelH);
  const BLRect contentRect(48.0,
                           panelY + 24.0,
                           std::max(80.0, width - 96.0),
                           std::max(80.0, panelH - 48.0));

  ctx.set_fill_style(BLRgba32(0xFFFFFFFFu));
  ctx.fill_round_rect(BLRoundRect(panelRect.x, panelRect.y, panelRect.w, panelRect.h, 8));
  ctx.set_stroke_style(BLRgba32(0xFFD0D7E2u));
  ctx.stroke_round_rect(BLRoundRect(panelRect.x, panelRect.y, panelRect.w, panelRect.h, 8));

  if (panel_) {
    panel_->render(*this, renderer, contentRect, seconds, pulse);
  }
}

}  // namespace Blend2DUI
