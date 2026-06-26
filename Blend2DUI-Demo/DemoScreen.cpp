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
      "StrokeWidth:1, InnerShadowColour:#18201700, InnerShadowWidth:10, InnerShadowOffsetX:-1, InnerShadowOffsetY:2, "
      "TextColour:#0F172A, Corner:8, Font:DejaVuSans, FontSize:14"};
  Blend2DUI::UI_ButtonStyleDefinition slider{
      "FillColour:#FFFFFF, HoverColour:#E0F2FE, PressedColour:#38BDF8, StrokeColour:#94A3B8, "
      "StrokeWidth:1, TextColour:#0F172A, Corner:8, Font:DejaVuSans, FontSize:14"};
  Blend2DUI::UI_ButtonStyleDefinition redSlider{
      "FillColour:#FFFFFF, HoverColour:#FEE2E2, PressedColour:#EF4444, StrokeColour:#F87171, "
      "StrokeWidth:1, TextColour:#0F172A, Corner:8, Font:DejaVuSans, FontSize:14"};
  Blend2DUI::UI_ButtonStyleDefinition label{
      "HasFill:false, HasStroke:false, TextColour:#0F172A, Font:DejaVuSans, FontSize:14"};
  Blend2DUI::UI_ButtonStyleDefinition labelStrong{
      "HasFill:false, HasStroke:false, TextColour:#0F172A, Font:DejaVuSans, FontSize:16, FontStyle:Bold"};
  Blend2DUI::UI_ButtonStyleDefinition toggle{
      "FillColour:#E5E7EB, HoverColour:#D1FAE5, PressedColour:#22C55E, StrokeColour:#86EFAC, "
      "StrokeWidth:1, TextColour:#0F172A, Corner:16, Font:DejaVuSans, FontSize:14"};
  Blend2DUI::UI_ButtonStyleDefinition imageFrame{
      "FillColour:#FFFFFF, HoverColour:#FFFFFF, PressedColour:#FFFFFF, StrokeColour:#CBD5E1, "
      "StrokeWidth:1, TextColour:#0F172A, Corner:12"};
  Blend2DUI::UI_ButtonStyleDefinition contextMenu{
      "FillColour:#FFFFFF, HoverColour:#FFFFFF, PressedColour:#FFFFFF, StrokeColour:#CBD5E1, "
      "StrokeWidth:1, ShadowColour:#260F172A, ShadowWidth:10, Corner:10, TextColour:#0F172A"};
  Blend2DUI::UI_ButtonStyleDefinition contextMenuItem{
      "FillColour:#FFFFFF, HoverColour:#EFF6FF, PressedColour:#DBEAFE, StrokeColour:#FFFFFF, "
      "StrokeWidth:0, HasStroke:false, Corner:8, TextColour:#0F172A, Font:DejaVuSans, FontSize:14"};
  Blend2DUI::UI_RectStyleDefinition menuRect{
      "Padding:16, RectFill:#FFFFFF, RectStroke:#D0D7E2, RectStrokeWidth:1, RectCorner:8"};
  Blend2DUI::UI_RectStyleDefinition inputRect{"Padding:0"};
  Blend2DUI::UI_TextInputOptions singleLineOptions{
      "Mode:SingleLine, MaxLength:256, Placeholder:'Single line text'"};
  Blend2DUI::UI_TextInputOptions multiLineOptions{
      "Mode:MultiLine, Resizable:true, Placeholder:'Multi-line text'"};
  Blend2DUI::UI_SliderOptions horizontalSliderOptions{
      "Heading:'Horizontal slider', Min:0, Max:100, Default:64, Step:1, Integer:true, Thumb:Circle"};
  Blend2DUI::UI_SliderOptions redHorizontalSliderOptions{
      "Heading:'Horizontal slider (red)', Min:0, Max:100, Default:28, Step:1, Integer:true, Thumb:Circle"};
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

std::vector<Blend2DUI::UI_ContextMenuItem> buildDemoContextMenuItems() {
  return {
      {Blend2DUI::UI_ButtonContent("Open", "", "assets/add-folder-blue.svg"), true},
      {Blend2DUI::UI_ButtonContent("Duplicate"), true},
      {Blend2DUI::UI_ButtonContent("Rename"), false},
      {Blend2DUI::UI_ButtonContent("Delete"), true},
  };
}

std::string demoContextMenuLabel(int index) {
  switch (index) {
    case 0: return "Open";
    case 1: return "Duplicate";
    case 2: return "Rename";
    case 3: return "Delete";
    default: return "none";
  }
}

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
    BLContext& canvas = renderer.context();
    canvas.set_fill_style(BLRgba32(0xFF111827u));
    canvas.fill_round_rect(BLRoundRect(rect.x, rect.y, rect.w, rect.h, 6));

    BLGradient orb(BLRadialGradientValues(rect.x + rect.w * (0.1 + 0.3 * pulse),
                                          rect.y + rect.h * 0.5,
                                          rect.x + rect.w * 0.25,
                                          rect.y + rect.h * 0.35,
                                          std::min(rect.w, rect.h) * 0.45));
    orb.add_stop(0.0, BLRgba32(0xFFFFF7ADu));
    orb.add_stop(0.45, BLRgba32(0xFF38BDF8u));
    orb.add_stop(1.0, BLRgba32(0x00111827u));
    canvas.set_fill_style(orb);
    canvas.fill_round_rect(BLRoundRect(rect.x, rect.y, rect.w, rect.h, 6));

    if (!screen.selectedFilePath().empty()) {
      canvas.set_fill_style(BLRgba32(0xF0FFFFFFu));
      canvas.fill_round_rect(BLRoundRect(rect.x + 18.0,
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
  renderer.UI_CursorGap(10);
  renderer.UI_Slider("horizontal-slider-demo-red",
                     "90%x58",
                     kDemoStyles.redHorizontalSliderOptions,
                     redHorizontalSliderValue_,
                     kDemoStyles.redSlider);

  renderer.UI_CursorUse("after-horizontal-slider");
  renderer.UI_CursorOffset(std::max(0.0, width - 154.0), -70.0);
  renderer.UI_Slider("vertical-slider-demo",
                     "66x168",
                     kDemoStyles.verticalSliderOptions,
                     verticalSliderValue_,
                     kDemoStyles.slider);
}

void DemoScreen::renderWidgetShowcase(SceneRenderer& renderer, const BLRect& rect) {
  const std::vector<Blend2DUI::UI_ContextMenuItem> menuItems = buildDemoContextMenuItems();
  UI_RectArea showcaseArea;
  showcaseArea.SetRect(rect, kDemoStyles.menuRect);
  renderer.UI_CursorRect(showcaseArea);
  renderer.UI_CursorTop(4);

  renderer.UI_Label("widget-showcase.title",
                    "100%x20",
                    kDemoStyles.labelStrong,
                    UI_ButtonContent("ButtonStyle Widgets"));
  renderer.UI_Label("widget-showcase.summary",
                    "100%x18",
                    kDemoStyles.label,
                    UI_ButtonContent(contextMenuSelection_ >= 0
                                         ? "Last menu action: " + demoContextMenuLabel(contextMenuSelection_)
                                         : "Last menu action: none"));

  renderer.UI_TickBox("widget-showcase.tickbox",
                      "100%x26",
                      tickBoxChecked_,
                      kDemoStyles.neutral,
                      UI_ButtonContent("Tickbox"));
  renderer.UI_Toggle("widget-showcase.toggle",
                     "100%x26",
                     toggleEnabled_,
                     kDemoStyles.toggle,
                     UI_ButtonContent("iOS-style toggle"));
  renderer.UI_Image("widget-showcase.image",
                    "100%x54",
                    kDemoStyles.imageFrame,
                    UI_ButtonContent("", "", "assets/Heaven.jpg"));

  UI_ContextMenuOptions menuOptions;
  menuOptions.menuWidth = std::max(172.0, showcaseArea.GetDrawableArea().w);
  menuOptions.itemHeight = 32.0;
  const int selection = renderer.UI_ContextMenu("widget-showcase.menu",
                                                "100%x32",
                                                kDemoStyles.neutral,
                                                UI_ButtonContent("Context menu"),
                                                menuItems,
                                                kDemoStyles.contextMenu,
                                                kDemoStyles.contextMenuItem,
                                                menuOptions);
  if (selection >= 0) {
    contextMenuSelection_ = selection;
  }
}

void DemoScreen::renderPanel(SceneRenderer& renderer,
                             double seconds,
                             double pulse,
                             double width,
                             double height) {
  DemoProfileScope sectionProfile(renderer, "demo.panel");
  auto containsPoint = [](const BLRect& rect, double x, double y) {
    return x >= rect.x && y >= rect.y && x <= rect.x + rect.w && y <= rect.y + rect.h;
  };

  BLContext& canvas = renderer.context();
  const double panelY = 498.0;
  const double panelH = std::max(120.0, height - panelY - 32.0);
  const BLRect panelRect(28, panelY, std::max(80.0, width - 56.0), panelH);
  const BLRect contentRect(48.0,
                           panelY + 24.0,
                           std::max(80.0, width - 96.0),
                           std::max(80.0, panelH - 48.0));
  const double showcaseGap = 20.0;
  const double showcaseWidth = std::min(260.0, std::max(220.0, contentRect.w * 0.34));
  const BLRect previewAreaRect(contentRect.x,
                               contentRect.y,
                               std::max(80.0, contentRect.w - showcaseWidth - showcaseGap),
                               contentRect.h);
  const BLRect showcaseRect(previewAreaRect.x + previewAreaRect.w + showcaseGap,
                              contentRect.y,
                              showcaseWidth,
                              std::min(contentRect.h, 232.0));

  const double canvas3DGap = previewAreaRect.w >= 260.0 ? 20.0 : 12.0;
  const double minOrbWidth = 60.0;
  const double minCanvas3DWidth = 96.0;
  const double availablePreviewWidth = std::max(0.0, previewAreaRect.w - canvas3DGap);
  const double minSplitRatio = availablePreviewWidth > 0.0 ? std::min(0.9, minOrbWidth / availablePreviewWidth) : 0.5;
  const double maxSplitRatio = availablePreviewWidth > 0.0 ? std::max(minSplitRatio, 1.0 - (minCanvas3DWidth / availablePreviewWidth)) : minSplitRatio;
  previewSplitRatio_ = std::clamp(previewSplitRatio_, minSplitRatio, maxSplitRatio);

  double orbWidth = std::clamp(availablePreviewWidth * previewSplitRatio_, minOrbWidth, std::max(minOrbWidth, availablePreviewWidth - minCanvas3DWidth));
  double canvas3DWidth = std::max(minCanvas3DWidth, previewAreaRect.w - orbWidth - canvas3DGap);
  double dividerX = previewAreaRect.x + orbWidth;
  BLRect dividerRect(dividerX,
                     previewAreaRect.y,
                     canvas3DGap,
                     previewAreaRect.h);
  BLRect dividerHitRect(dividerRect.x - 4.0,
                        dividerRect.y,
                        dividerRect.w + 8.0,
                        dividerRect.h);

  if (renderer.mousePressed() && containsPoint(dividerHitRect, renderer.mouseX(), renderer.mouseY())) {
    draggingPreviewDivider_ = true;
    previewDividerGrabOffset_ = renderer.mouseX() - dividerRect.x;
  }
  if (draggingPreviewDivider_ && renderer.mouseDown()) {
    const double minDividerX = previewAreaRect.x + minOrbWidth;
    const double maxDividerX = previewAreaRect.x + previewAreaRect.w - canvas3DGap - minCanvas3DWidth;
    dividerX = std::clamp(renderer.mouseX() - previewDividerGrabOffset_, minDividerX, maxDividerX);
    orbWidth = dividerX - previewAreaRect.x;
    canvas3DWidth = std::max(minCanvas3DWidth, previewAreaRect.w - orbWidth - canvas3DGap);
    previewSplitRatio_ = availablePreviewWidth > 0.0 ? orbWidth / availablePreviewWidth : previewSplitRatio_;
    dividerRect = BLRect(dividerX, previewAreaRect.y, canvas3DGap, previewAreaRect.h);
    dividerHitRect = BLRect(dividerRect.x - 4.0, dividerRect.y, dividerRect.w + 8.0, dividerRect.h);
  }
  if (draggingPreviewDivider_ && renderer.mouseReleased()) {
    draggingPreviewDivider_ = false;
  }

  const BLRect previewRect(previewAreaRect.x,
                           previewAreaRect.y,
                           orbWidth,
                           previewAreaRect.h);
  const BLRect canvas3DPanelRect(previewRect.x + previewRect.w + canvas3DGap,
                                 previewAreaRect.y,
                                 std::max(20.0, canvas3DWidth),
                                 previewAreaRect.h);
  const BLRect canvas3DViewport(canvas3DPanelRect.x + 10.0,
                                canvas3DPanelRect.y + 10.0,
                                std::max(20.0, canvas3DPanelRect.w - 20.0),
                                std::max(20.0, canvas3DPanelRect.h - 20.0));

  canvas.set_fill_style(BLRgba32(0xFFFFFFFFu));
  canvas.fill_round_rect(BLRoundRect(panelRect.x, panelRect.y, panelRect.w, panelRect.h, 8));
  canvas.set_stroke_style(BLRgba32(0xFFD0D7E2u));
  canvas.stroke_round_rect(BLRoundRect(panelRect.x, panelRect.y, panelRect.w, panelRect.h, 8));

  if (panel_) {
    panel_->render(*this, renderer, previewRect, seconds, pulse);
  }

  const bool dividerHovered = containsPoint(dividerHitRect, renderer.mouseX(), renderer.mouseY());
  const uint32_t dividerColour = draggingPreviewDivider_ ? 0xFF38BDF8u : dividerHovered ? 0xFF94A3B8u : 0xFFE2E8F0u;
  canvas.set_fill_style(BLRgba32(dividerColour));
  canvas.fill_round_rect(BLRoundRect(dividerRect.x + (dividerRect.w - 6.0) * 0.5,
                                     dividerRect.y + 14.0,
                                     6.0,
                                     std::max(24.0, dividerRect.h - 28.0),
                                     3.0));
  canvas.set_fill_style(BLRgba32(0xFFFFFFFFu));
  for (int i = 0; i < 3; ++i) {
    canvas.fill_circle(BLCircle(dividerRect.x + dividerRect.w * 0.5,
                                dividerRect.y + dividerRect.h * 0.5 + (static_cast<double>(i) - 1.0) * 9.0,
                                1.6));
  }

  canvas.set_fill_style(BLRgba32(0xFF0F172Au));
  canvas.fill_round_rect(BLRoundRect(canvas3DPanelRect.x, canvas3DPanelRect.y, canvas3DPanelRect.w, canvas3DPanelRect.h, 12));
  canvas.set_stroke_style(BLRgba32(0xFF334155u));
  canvas.stroke_round_rect(BLRoundRect(canvas3DPanelRect.x, canvas3DPanelRect.y, canvas3DPanelRect.w, canvas3DPanelRect.h, 12));
  canvas3D_.render(renderer, canvas3DViewport, seconds);

  renderWidgetShowcase(renderer, showcaseRect);
}

}  // namespace Blend2DUI
