#include "Blend2DUI/SdlBlend2DRenderer.h"

#include <iostream>

namespace Blend2DUI {

SdlBlend2DRenderer::~SdlBlend2DRenderer() {
  shutdown();
}

bool SdlBlend2DRenderer::initialize(const std::string& title, int width, int height) {
  if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
    std::cerr << "SDL_Init failed: " << SDL_GetError() << "\n";
    return false;
  }

  window_ = SDL_CreateWindow(title.c_str(), width, height, SDL_WINDOW_RESIZABLE);
  if (!window_) {
    std::cerr << "SDL_CreateWindow failed: " << SDL_GetError() << "\n";
    shutdown();
    return false;
  }

  renderer_ = SDL_CreateRenderer(window_, nullptr);
  if (!renderer_) {
    std::cerr << "SDL_CreateRenderer failed: " << SDL_GetError() << "\n";
    shutdown();
    return false;
  }

  buttonResources_.images = &imageCache_;
  buttonResources_.fonts = &fontCache_;
  buttonResources_.assetBasePath = assetBasePath_;
  SDL_StartTextInput(window_);

  return ensureBackBuffer();
}

void SdlBlend2DRenderer::shutdown() {
  if (frameActive_) {
    context_.end();
    frameActive_ = false;
  }
  if (texture_) {
    SDL_DestroyTexture(texture_);
    texture_ = nullptr;
  }
  if (renderer_) {
    SDL_DestroyRenderer(renderer_);
    renderer_ = nullptr;
  }
  if (window_) {
    SDL_DestroyWindow(window_);
    window_ = nullptr;
  }
  SDL_Quit();
  width_ = 0;
  height_ = 0;
  image_.reset();
  imageCache_.clear();
  fontCache_.clear();
  buttonResources_ = UI_ButtonResources();
}

bool SdlBlend2DRenderer::handleEvent(const SDL_Event& event) {
  if (event.type == SDL_EVENT_QUIT) {
    return false;
  }

  if (event.type == SDL_EVENT_MOUSE_MOTION) {
    mouseX_ = event.motion.x;
    mouseY_ = event.motion.y;
  } else if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN && event.button.button == SDL_BUTTON_LEFT) {
    mouseX_ = event.button.x;
    mouseY_ = event.button.y;
    mouseDown_ = true;
    mousePressed_ = true;
  } else if (event.type == SDL_EVENT_MOUSE_BUTTON_UP && event.button.button == SDL_BUTTON_LEFT) {
    mouseX_ = event.button.x;
    mouseY_ = event.button.y;
    mouseDown_ = false;
    mouseReleased_ = true;
  } else if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN && event.button.button == SDL_BUTTON_RIGHT) {
    mouseX_ = event.button.x;
    mouseY_ = event.button.y;
    rightMousePressed_ = true;
  } else if (event.type == SDL_EVENT_MOUSE_BUTTON_UP && event.button.button == SDL_BUTTON_RIGHT) {
    mouseX_ = event.button.x;
    mouseY_ = event.button.y;
    rightMouseReleased_ = true;
  } else if (event.type == SDL_EVENT_WINDOW_MOUSE_LEAVE) {
    hoveredButtonId_.clear();
  } else if (event.type == SDL_EVENT_TEXT_INPUT) {
    if (event.text.text) textInputEvents_.emplace_back(event.text.text);
  } else if (event.type == SDL_EVENT_KEY_DOWN) {
    keyEvents_.push_back(UI_TextInputKeyEvent{event.key.key, event.key.mod, event.key.repeat});
  } else if (event.type == SDL_EVENT_MOUSE_WHEEL) {
    wheelY_ += event.wheel.y;
  }

  return true;
}

bool SdlBlend2DRenderer::ensureBackBuffer() {
  if (!window_) return false;

  int pixelWidth = 0;
  int pixelHeight = 0;
  SDL_GetWindowSizeInPixels(window_, &pixelWidth, &pixelHeight);
  if (pixelWidth <= 0 || pixelHeight <= 0) return false;

  if (pixelWidth == width_ && pixelHeight == height_ && texture_) {
    return true;
  }

  return resizeBackBuffer(pixelWidth, pixelHeight);
}

bool SdlBlend2DRenderer::resizeBackBuffer(int width, int height) {
  if (texture_) {
    SDL_DestroyTexture(texture_);
    texture_ = nullptr;
  }

  texture_ = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, width, height);
  if (!texture_) {
    std::cerr << "SDL_CreateTexture failed: " << SDL_GetError() << "\n";
    return false;
  }

  SDL_SetTextureBlendMode(texture_, SDL_BLENDMODE_BLEND);
  image_.create(width, height, BL_FORMAT_PRGB32);
  width_ = width;
  height_ = height;
  return true;
}

bool SdlBlend2DRenderer::beginFrame(double seconds) {
  if (!ensureBackBuffer()) return false;
  if (frameActive_) {
    context_.end();
    frameActive_ = false;
  }

  frameSeconds_ = seconds;
  modalPointerCaptureActive_ = nextModalPointerCaptureActive_;
  modalPointerCaptureIdPrefix_ = nextModalPointerCaptureIdPrefix_;
  nextModalPointerCaptureActive_ = false;
  nextModalPointerCaptureIdPrefix_.clear();
  context_.begin(image_);
  frameActive_ = true;

  context_.set_comp_op(BL_COMP_OP_SRC_COPY);
  context_.fill_all(BLRgba32(0xFFF7F8FAu));
  context_.set_comp_op(BL_COMP_OP_SRC_OVER);
  return true;
}

bool SdlBlend2DRenderer::endFrame() {
  if (!frameActive_) return false;
  context_.end();
  frameActive_ = false;

  mousePressed_ = false;
  mouseReleased_ = false;
  rightMousePressed_ = false;
  rightMouseReleased_ = false;
  wheelY_ = 0.0;
  textInputEvents_.clear();
  keyEvents_.clear();
  return uploadBlend2DImage();
}

bool SdlBlend2DRenderer::uploadBlend2DImage() {
  BLImageData data;
  if (image_.get_data(&data) != BL_SUCCESS) {
    std::cerr << "BLImage::get_data failed\n";
    return false;
  }

  if (!SDL_UpdateTexture(texture_, nullptr, data.pixel_data, static_cast<int>(data.stride))) {
    std::cerr << "SDL_UpdateTexture failed: " << SDL_GetError() << "\n";
    return false;
  }
  return true;
}

void SdlBlend2DRenderer::present() {
  if (!renderer_ || !texture_) return;
  SDL_SetRenderDrawColor(renderer_, 17, 24, 39, 255);
  SDL_RenderClear(renderer_);
  SDL_RenderTexture(renderer_, texture_, nullptr, nullptr);
  SDL_RenderPresent(renderer_);
}

}  // namespace Blend2DUI
