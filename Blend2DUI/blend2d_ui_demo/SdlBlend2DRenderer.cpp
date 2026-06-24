#include "Blend2DUI/SdlBlend2DRenderer.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <iostream>
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

const DemoButtonStyles ButStyles;

class DemoProfileScope {
 public:
  DemoProfileScope(Blend2DUI::SdlBlend2DRenderer& renderer, std::string name)
      : renderer_(renderer), name_(std::move(name)), active_(renderer_.profilingEnabled()) {
    if (active_) start_ = std::chrono::steady_clock::now();
  }

  ~DemoProfileScope() {
    if (!active_) return;
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start_).count();
    renderer_.profileSection(name_, ms);
  }

 private:
  Blend2DUI::SdlBlend2DRenderer& renderer_;
  std::string name_;
  bool active_ = false;
  std::chrono::steady_clock::time_point start_;
};

Blend2DUI::UI_FileDialogResult ShowDialog(Blend2DUI::SdlBlend2DRenderer& renderer,
                                          Blend2DUI::UI_FileDialogMode fileDialogMode,
                                          std::string& selectedFilePath,
                                          const double w,
                                          const double h) {
  Blend2DUI::UI_FileDialogOptions dialogOptions;
  dialogOptions.mode = fileDialogMode;
  dialogOptions.title = fileDialogMode == Blend2DUI::UI_FileDialogMode::Save ? "Save Blend2D Output" : "Load File";
  if (const char* home = std::getenv("HOME")) {
    std::filesystem::path start = home;
    const std::filesystem::path documents = start / "Documents";
    if (std::filesystem::is_directory(documents)) start = documents;
    dialogOptions.startPath = start;
  }
  dialogOptions.filters = {
      {"All files", "*.*"},
      {"PNG images", "*.png"},
      {"SVG files", "*.svg"},
      {"Text files", "*.txt"},
  };
  const double dialogW = std::min(820.0, std::max(560.0, w - 70.0));
  const double dialogH = std::min(560.0, std::max(430.0, h - 56.0));
  const BLRect dialogRect((w - dialogW) * 0.5, (h - dialogH) * 0.5, dialogW, dialogH);
  return renderer.UI_FileDialog("demo-file-dialog", dialogRect, dialogOptions, selectedFilePath);
}

}  // namespace

namespace Blend2DUI {

bool SdlBlend2DRenderer::renderDemoFrame(double seconds) {
  DemoProfileScope renderProfile(*this, "demo.renderFrame");
  if (!beginFrame(seconds)) return false;

  static bool showFileDialog = false;
  bool openedFileDialogThisFrame = false;
  static Blend2DUI::UI_FileDialogMode fileDialogMode = Blend2DUI::UI_FileDialogMode::Open;
  static std::string selectedFilePath;

  BLContext& ctx = context_;

  const double w = static_cast<double>(width_);
  const double h = static_cast<double>(height_);
  const double pulse = 0.5 + 0.5 * std::sin(seconds * 2.2);

  {
    DemoProfileScope sectionProfile(*this, "demo.menu");
    UI_RectArea menuArea;
    menuArea.SetRect(BLRect(28, 28, std::max(80.0, w - 56.0), 72), ButStyles.menuRect);
    UI_CursorRect(menuArea);
    UI_CursorLeft(14);

    if (UI_Button("1", "132x40", ButStyles.select, UI_ButtonContent("Select", "Select a shape")) == UI_ButtonActionPressed)
    {
        std::cout << "Select button pressed\n";
    }
    if (UI_Button("2", "170x40", ButStyles.gradient, UI_ButtonContent("Gradient", "Blend2D gradient button")) == UI_ButtonActionPressed)
    {
        std::cout << "Gradient button pressed\n";
    }
    if (UI_Button("3", "80x40", ButStyles.image, UI_ButtonContent("", "SVG icon button", "assets/add-folder-blue.svg")) == UI_ButtonActionPressed)
    {
        std::cout << "Image button pressed\n";
    }
    if (UI_Button("4", "104x40", ButStyles.neutral, UI_ButtonContent("Load...")) == UI_ButtonActionPressed)
    {
        fileDialogMode = Blend2DUI::UI_FileDialogMode::Open;
        showFileDialog = true;
        openedFileDialogThisFrame = true;
    }
    if (UI_Button("5", "104x40", ButStyles.neutral, UI_ButtonContent("Save...")) == UI_ButtonActionPressed)
    {
        fileDialogMode = Blend2DUI::UI_FileDialogMode::Save;
        showFileDialog = true;
        openedFileDialogThisFrame = true;
    }
  }

  static std::string singleLineText = "Single line input - paste or type UTF-8 text";
  UI_RectArea inputArea;
  inputArea.SetRect(BLRect(44, 116, std::max(260.0, w - 88.0), 362.0), ButStyles.inputRect);
  UI_CursorRect(inputArea);
  UI_CursorTop(12);

  static std::string multiLineText =
      "Multi-line text area with wrapping.\n"
      "Try Japanese: こんにちは世界\n"
      "Drag to select, Ctrl+C/Ctrl+V to copy and paste, arrows to move the caret.";
  {
    DemoProfileScope sectionProfile(*this, "demo.inputs");
    UI_TextInput("single-line-demo", "100%x40", ButStyles.singleLineOptions, singleLineText, ButStyles.input);
    UI_TextInput("multi-line-demo", "100%x128", ButStyles.multiLineOptions, multiLineText, ButStyles.input);
  }

  static double horizontalSliderValue = 64.0;
  {
    DemoProfileScope sliderProfile(*this, "demo.sliders");
    UI_CursorGap(2);
    UI_Slider("horizontal-slider-demo", "90%x58", ButStyles.horizontalSliderOptions, horizontalSliderValue, ButStyles.slider);

    static double verticalSliderValue = 0.35;
    UI_CursorSave("after-horizontal-slider");
    UI_CursorUse("after-horizontal-slider");
    UI_CursorOffset(std::max(0.0, w - 154.0), -70.0);
    UI_Slider("vertical-slider-demo", "66x168", ButStyles.verticalSliderOptions, verticalSliderValue, ButStyles.slider);
  }

  const double panelY = 498.0;
  const double panelH = std::max(120.0, h - panelY - 32.0);
  const double canvasX = 48.0;
  const double canvasY = panelY + 24.0;
  const double canvasW = std::max(80.0, w - 96.0);
  const double canvasH = std::max(80.0, panelH - 48.0);

  {
  DemoProfileScope sectionProfile(*this, "demo.canvas");
  ctx.set_fill_style(BLRgba32(0xFFFFFFFFu));
  ctx.fill_round_rect(BLRoundRect(28, panelY, std::max(80.0, w - 56.0), panelH, 8));
  ctx.set_stroke_style(BLRgba32(0xFFD0D7E2u));
  ctx.stroke_round_rect(BLRoundRect(28, panelY, std::max(80.0, w - 56.0), panelH, 8));


  ctx.set_fill_style(BLRgba32(0xFF111827u));
  ctx.fill_round_rect(BLRoundRect(canvasX, canvasY, canvasW, canvasH, 6));

  BLGradient orb(BLRadialGradientValues(canvasX + canvasW * (0.1 + 0.3 * pulse),
                                        canvasY + canvasH * 0.5,
                                        canvasX + canvasW * 0.25,
                                        canvasY + canvasH * 0.35,
                                        std::min(canvasW, canvasH) * 0.45));
  orb.add_stop(0.0, BLRgba32(0xFFFFF7ADu));
  orb.add_stop(0.45, BLRgba32(0xFF38BDF8u));
  orb.add_stop(1.0, BLRgba32(0x00111827u));
  ctx.set_fill_style(orb);
  ctx.fill_round_rect(BLRoundRect(canvasX, canvasY, canvasW, canvasH, 6));

  if (!selectedFilePath.empty()) {
    ctx.set_fill_style(BLRgba32(0xF0FFFFFFu));
    ctx.fill_round_rect(BLRoundRect(canvasX + 18.0, canvasY + canvasH - 54.0, std::min(560.0, canvasW - 36.0), 34.0, 6.0));
  }
  }

  if (showFileDialog && !openedFileDialogThisFrame) {
      DemoProfileScope sectionProfile(*this, "demo.fileDialog");
      Blend2DUI::UI_FileDialogResult result = ShowDialog(*this, fileDialogMode, selectedFilePath, w, h);
      if (result == Blend2DUI::UI_FileDialogResult::Accepted) {
          std::cout << (fileDialogMode == Blend2DUI::UI_FileDialogMode::Save ? "Save path: " : "Load path: ") << selectedFilePath << "\n";
          showFileDialog = false;
      }
      else if (result == Blend2DUI::UI_FileDialogResult::Cancelled) {
          showFileDialog = false;
      }
  }

  return endFrame();
}

}  // namespace Blend2DUI
