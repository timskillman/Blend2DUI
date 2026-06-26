#pragma once

#include "Button.h"
#include "FileDialog.h"
#include "Layout.h"
#include "ShapedTextCache.h"
#include "Slider.h"
#include "TextInput.h"

#include <SDL3/SDL.h>
#include <blend2d/blend2d.h>

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace Blend2DUI {

class Canvas3D;

struct UI_ProfileBucket {
  double totalMs = 0.0;
  double maxMs = 0.0;
  int samples = 0;
};

struct UI_ContextMenuItem {
  UI_ButtonContent content;
  bool enabled = true;
};

struct UI_ContextMenuOptions {
  double menuWidth = 184.0;
  double itemHeight = 34.0;
  double padding = 6.0;
  double popupOffset = 4.0;
  bool openOnLeftClick = true;
  bool openOnRightClick = true;
};

class SceneRenderer {
 public:
  SceneRenderer() = default;
  ~SceneRenderer();

  SceneRenderer(const SceneRenderer&) = delete;
  SceneRenderer& operator=(const SceneRenderer&) = delete;

  bool initialize(const std::string& title, int width, int height);
  void setAssetBasePath(std::string assetBasePath);
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
  bool UI_TickBox(const std::string& id,
                  const BLRect& rect,
                  bool& checked,
                  const UI_ButtonStyleDefinition& style,
                  const UI_ButtonContent& content = UI_ButtonContent{});
  bool UI_TickBox(const std::string& id,
                  const std::string& size,
                  bool& checked,
                  const UI_ButtonStyleDefinition& style,
                  const UI_ButtonContent& content = UI_ButtonContent{});
  bool UI_Toggle(const std::string& id,
                 const BLRect& rect,
                 bool& enabled,
                 const UI_ButtonStyleDefinition& style,
                 const UI_ButtonContent& content = UI_ButtonContent{});
  bool UI_Toggle(const std::string& id,
                 const std::string& size,
                 bool& enabled,
                 const UI_ButtonStyleDefinition& style,
                 const UI_ButtonContent& content = UI_ButtonContent{});
  void UI_Label(const std::string& id,
                const BLRect& rect,
                const UI_ButtonStyleDefinition& style,
                const UI_ButtonContent& content = UI_ButtonContent{});
  void UI_Label(const std::string& id,
                const std::string& size,
                const UI_ButtonStyleDefinition& style,
                const UI_ButtonContent& content = UI_ButtonContent{});
  void UI_Image(const std::string& id,
                const BLRect& rect,
                const UI_ButtonStyleDefinition& style,
                const UI_ButtonContent& content);
  void UI_Image(const std::string& id,
                const std::string& size,
                const UI_ButtonStyleDefinition& style,
                const UI_ButtonContent& content);
  int UI_ContextMenu(const std::string& id,
                     const BLRect& rect,
                     const UI_ButtonStyleDefinition& triggerStyle,
                     const UI_ButtonContent& triggerContent,
                     const std::vector<UI_ContextMenuItem>& items,
                     const UI_ButtonStyleDefinition& menuStyle,
                     const UI_ButtonStyleDefinition& itemStyle,
                     const UI_ContextMenuOptions& options = UI_ContextMenuOptions{});
  int UI_ContextMenu(const std::string& id,
                     const std::string& size,
                     const UI_ButtonStyleDefinition& triggerStyle,
                     const UI_ButtonContent& triggerContent,
                     const std::vector<UI_ContextMenuItem>& items,
                     const UI_ButtonStyleDefinition& menuStyle,
                     const UI_ButtonStyleDefinition& itemStyle,
                     const UI_ContextMenuOptions& options = UI_ContextMenuOptions{});
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
  void queueCanvas3D(Canvas3D& canvas, const BLRect& rect, double seconds);
  UI_FileDialogResult UI_FileDialog(const std::string& id,
                                    const BLRect& rect,
                                    const UI_FileDialogOptions& options,
                                    std::string& selectedPath);
  bool endFrame();
  void present();
  bool profilingEnabled() const { return profilingEnabled_; }
  void profileSection(const std::string& name, double elapsedMs);

  BLContext& context() { return context_; }
  const BLContext& context() const { return context_; }
  int width() const { return width_; }
  int height() const { return height_; }
  double mouseX() const { return mouseX_; }
  double mouseY() const { return mouseY_; }
  bool mouseDown() const { return mouseDown_; }
  bool mousePressed() const { return mousePressed_; }
  bool mouseReleased() const { return mouseReleased_; }

 private:
  friend class Canvas3D;

  bool ensureBackBuffer();
  bool resizeBackBuffer(int width, int height);
  bool initializeOpenGL();
  bool ensurePresentationResources();
  void destroyPresentationResources();
  bool uploadBlend2DImage();
  bool pointerCapturedByModal(const std::string& id) const;
  UI_Size resolveLayoutSize(const std::string& size) const;
  BLRect layoutNextRect(double width, double height);
  bool layoutRectVisible(const BLRect& rect) const;
  bool layoutMouseInside() const;
  void profileMaybeReport();

  struct UI_ContextMenuState {
    bool open = false;
    double x = 0.0;
    double y = 0.0;
  };

  struct Canvas3DRequest {
    Canvas3D* canvas = nullptr;
    BLRect rect;
    double seconds = 0.0;
  };

  SDL_Window* window_ = nullptr;
  SDL_GLContext glContext_ = nullptr;
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
  bool modalOverlayActive_ = false;
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
  std::unordered_map<std::string, UI_ContextMenuState> contextMenuStates_;
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
  bool glReady_ = false;
  unsigned int glBackBufferTexture_ = 0;
  unsigned int glPresentationProgram_ = 0;
  unsigned int glPresentationVbo_ = 0;
  int glPresentationTextureUniform_ = -1;
  std::vector<unsigned char> uploadBuffer_;
  std::vector<Canvas3DRequest> canvas3DRequests_;
};

}  // namespace Blend2DUI
