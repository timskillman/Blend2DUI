#pragma once

#include "Blend2DUI/Button.h"
#include "Blend2DUI/FileDialog.h"
#include "Blend2DUI/TextInput.h"

#include <SDL3/SDL.h>
#include <blend2d/blend2d.h>

#include <string>
#include <unordered_map>
#include <vector>

namespace Blend2DUI {

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
  UI_ButtonAction UI_Button(const std::string& id,
                           const BLRect& rect,
                           const UI_ButtonStyleDefinition& style,
                           const UI_ButtonContent& content = UI_ButtonContent{});
  bool UI_TextInput(const std::string& id,
                    const BLRect& rect,
                    const UI_TextInputOptions& options,
                    std::string& text,
                    const UI_ButtonStyleDefinition& style);
  UI_FileDialogResult UI_FileDialog(const std::string& id,
                                    const BLRect& rect,
                                    const UI_FileDialogOptions& options,
                                    std::string& selectedPath);
  bool endFrame();
  bool renderDemoFrame(double seconds);
  void present();

  int width() const { return width_; }
  int height() const { return height_; }

 private:
  bool ensureBackBuffer();
  bool resizeBackBuffer(int width, int height);
  bool uploadBlend2DImage();
  bool pointerCapturedByModal(const std::string& id) const;

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
  double hoverStartSeconds_ = 0.0;
  std::unordered_map<std::string, UI_TextInputState> textInputStates_;
  std::unordered_map<std::string, UI_FileDialogState> fileDialogStates_;
  std::unordered_map<std::string, BLImage> imageCache_;
  std::unordered_map<std::string, BLFontFace> fontCache_;
  std::string assetBasePath_ = ".";
  UI_ButtonResources buttonResources_;
  int width_ = 0;
  int height_ = 0;
};

}  // namespace Blend2DUI
