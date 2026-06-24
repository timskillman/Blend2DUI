#pragma once

#include "Blend2DUI/Button.h"
#include "Blend2DUI/FileDialog.h"
#include "Blend2DUI/Layout.h"
#include "Blend2DUI/ShapedTextCache.h"
#include "Blend2DUI/Slider.h"
#include "Blend2DUI/TextInput.h"

#include <SDL3/SDL.h>
#include <blend2d/blend2d.h>

#include <string>
#include <unordered_map>
#include <vector>

namespace Blend2DUI {

struct UI_ProfileBucket {
  double totalMs = 0.0;
  double maxMs = 0.0;
  int samples = 0;
};

class SdlBlend2DRenderer {
 public:
  SdlBlend2DRenderer() = default;
  ~SdlBlend2DRenderer();

  SdlBlend2DRenderer(const SdlBlend2DRenderer&) = delete;
  SdlBlend2DRenderer& operator=(const SdlBlend2DRenderer&) = delete;

  bool initialize(const std::string& title, int width, int height);
  void shutdown();

  bool handleEvent(const SDL_Event& event);
  bool beginFrame(double seconds);
  void UI_CursorRect(const UI_RectArea& area);
  void UI_CursorSave(const std::string& id);
  void UI_CursorUse(const std::string& id);
  void UI_CursorOffset(double x, double y);
  void UI_CursorNext();
  void UI_CursorLeft(int gap = 3);
  void UI_CursorRight(int gap = 3);
  void UI_CursorBottom(int gap = 3);
  void UI_CursorTop(int gap = 3);
  void UI_CursorVerticalCenter();
  void UI_CursorHorizontalCenter();
  void UI_CursorLine();
  void UI_CursorGap(int gap);
  UI_ButtonAction UI_Button(const std::string& id,
                           const BLRect& rect,
                           const UI_ButtonStyleDefinition& style,
                           const UI_ButtonContent& content = UI_ButtonContent{});
  UI_ButtonAction UI_Button(const std::string& id,
                           const std::string& size,
                           const UI_ButtonStyleDefinition& style,
                           const UI_ButtonContent& content = UI_ButtonContent{});
  bool UI_TextInput(const std::string& id,
                    const BLRect& rect,
                    const UI_TextInputOptions& options,
                    std::string& text,
                    const UI_ButtonStyleDefinition& style);
  bool UI_TextInput(const std::string& id,
                    const std::string& size,
                    const UI_TextInputOptions& options,
                    std::string& text,
                    const UI_ButtonStyleDefinition& style);
  bool UI_Slider(const std::string& id,
                 const BLRect& rect,
                 const UI_SliderOptions& options,
                 double& value,
                 const UI_ButtonStyleDefinition& style);
  bool UI_Slider(const std::string& id,
                 const std::string& size,
                 const UI_SliderOptions& options,
                 double& value,
                 const UI_ButtonStyleDefinition& style);
  UI_FileDialogResult UI_FileDialog(const std::string& id,
                                    const BLRect& rect,
                                    const UI_FileDialogOptions& options,
                                    std::string& selectedPath);
  bool endFrame();
  bool renderDemoFrame(double seconds);
  void present();
  bool profilingEnabled() const { return profilingEnabled_; }
  void profileSection(const std::string& name, double elapsedMs);

  int width() const { return width_; }
  int height() const { return height_; }

 private:
  bool ensureBackBuffer();
  bool resizeBackBuffer(int width, int height);
  bool uploadBlend2DImage();
  bool pointerCapturedByModal(const std::string& id) const;
  UI_Size resolveLayoutSize(const std::string& size) const;
  BLRect layoutNextRect(double width, double height);
  bool layoutRectVisible(const BLRect& rect) const;
  bool layoutMouseInside() const;
  void profileMaybeReport();

  SDL_Window* window_ = nullptr;
  SDL_Renderer* renderer_ = nullptr;
  SDL_Texture* texture_ = nullptr;
  BLImage image_;
  BLContext context_;
  bool frameActive_ = false;
  double mouseX_ = 0.0;
  double mouseY_ = 0.0;
  bool mouseDown_ = false;
  bool mousePressed_ = false;
  bool mouseReleased_ = false;
  bool rightMousePressed_ = false;
  bool rightMouseReleased_ = false;
  double frameSeconds_ = 0.0;
  double wheelY_ = 0.0;
  bool modalPointerCaptureActive_ = false;
  bool nextModalPointerCaptureActive_ = false;
  std::string modalPointerCaptureIdPrefix_;
  std::string nextModalPointerCaptureIdPrefix_;
  std::vector<std::string> textInputEvents_;
  std::vector<UI_TextInputKeyEvent> keyEvents_;
  std::string activeButtonId_;
  std::string hoveredButtonId_;
  std::string activeTextInputId_;
  std::string focusedTextInputId_;
  std::string activeSliderId_;
  std::string focusedSliderId_;
  double hoverStartSeconds_ = 0.0;
  std::unordered_map<std::string, UI_TextInputState> textInputStates_;
  std::unordered_map<std::string, UI_SliderState> sliderStates_;
  std::unordered_map<std::string, UI_FileDialogState> fileDialogStates_;
  std::unordered_map<std::string, BLImage> imageCache_;
  std::unordered_map<std::string, BLFontFace> fontCache_;
  UI_ShapedTextCache shapedTextCache_;
  std::string assetBasePath_ = ".";
  UI_ButtonResources buttonResources_;
  UI_CursorState cursor_;
  std::unordered_map<std::string, UI_CursorState> savedCursors_;
  bool profilingEnabled_ = false;
  int profileFrames_ = 0;
  std::unordered_map<std::string, UI_ProfileBucket> profileBuckets_;
  int width_ = 0;
  int height_ = 0;
};

}  // namespace Blend2DUI
