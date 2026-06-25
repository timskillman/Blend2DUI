#include "SceneRenderer.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <vector>

namespace Blend2DUI {
namespace {

using Clock = std::chrono::steady_clock;

double elapsedMs(Clock::time_point start) {
  return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}

bool profileEnvironmentEnabled() {
  const char* value = std::getenv("BLEND2DUI_PROFILE");
  if (!value) return false;
  const std::string text(value);
  return text == "1" || text == "true" || text == "TRUE" || text == "on" || text == "ON";
}

const char* profileLogPath() {
  const char* value = std::getenv("BLEND2DUI_PROFILE_LOG");
  return value && value[0] != '\0' ? value : "blend2d_ui_profile.log";
}

}  // namespace

SceneRenderer::~SceneRenderer() {
  shutdown();
}

bool SceneRenderer::initialize(const std::string& title, int width, int height) {
  profilingEnabled_ = profileEnvironmentEnabled();
  if (profilingEnabled_) {
    std::ofstream log(profileLogPath(), std::ios::trunc);
    log << "Blend2DUI profiling enabled\n";
  }
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
  buttonResources_.shapedText = &shapedTextCache_;
  buttonResources_.assetBasePath = assetBasePath_;
  SDL_StartTextInput(window_);

  return ensureBackBuffer();
}

void SceneRenderer::shutdown() {
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
  shapedTextCache_.clear();
  buttonResources_ = UI_ButtonResources();
}

bool SceneRenderer::handleEvent(const SDL_Event& event) {
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

bool SceneRenderer::ensureBackBuffer() {
  const auto profileStart = Clock::now();
  if (!window_) return false;

  int pixelWidth = 0;
  int pixelHeight = 0;
  SDL_GetWindowSizeInPixels(window_, &pixelWidth, &pixelHeight);
  if (pixelWidth <= 0 || pixelHeight <= 0) return false;

  if (pixelWidth == width_ && pixelHeight == height_ && texture_) {
    if (profilingEnabled_) profileSection("ensureBackBuffer", elapsedMs(profileStart));
    return true;
  }

  const bool resized = resizeBackBuffer(pixelWidth, pixelHeight);
  if (profilingEnabled_) profileSection("ensureBackBuffer", elapsedMs(profileStart));
  return resized;
}

bool SceneRenderer::resizeBackBuffer(int width, int height) {
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

bool SceneRenderer::beginFrame(double seconds) {
  const auto profileStart = Clock::now();
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
  if (profilingEnabled_) {
    ++profileFrames_;
    profileSection("beginFrame", elapsedMs(profileStart));
  }
  return true;
}

bool SceneRenderer::endFrame() {
  const auto profileStart = Clock::now();
  if (!frameActive_) return false;
  const auto contextEndStart = Clock::now();
  context_.end();
  if (profilingEnabled_) profileSection("contextEnd", elapsedMs(contextEndStart));
  frameActive_ = false;

  mousePressed_ = false;
  mouseReleased_ = false;
  rightMousePressed_ = false;
  rightMouseReleased_ = false;
  wheelY_ = 0.0;
  textInputEvents_.clear();
  keyEvents_.clear();
  const bool uploaded = uploadBlend2DImage();
  if (profilingEnabled_) {
    profileSection("endFrame", elapsedMs(profileStart));
    profileMaybeReport();
  }
  return uploaded;
}

bool SceneRenderer::uploadBlend2DImage() {
  const auto profileStart = Clock::now();
  BLImageData data;
  if (image_.get_data(&data) != BL_SUCCESS) {
    std::cerr << "BLImage::get_data failed\n";
    return false;
  }

  if (!SDL_UpdateTexture(texture_, nullptr, data.pixel_data, static_cast<int>(data.stride))) {
    std::cerr << "SDL_UpdateTexture failed: " << SDL_GetError() << "\n";
    return false;
  }
  if (profilingEnabled_) profileSection("uploadTexture", elapsedMs(profileStart));
  return true;
}

void SceneRenderer::present() {
  const auto profileStart = Clock::now();
  if (!renderer_ || !texture_) return;
  SDL_SetRenderDrawColor(renderer_, 17, 24, 39, 255);
  SDL_RenderClear(renderer_);
  SDL_RenderTexture(renderer_, texture_, nullptr, nullptr);
  SDL_RenderPresent(renderer_);
  if (profilingEnabled_) profileSection("present", elapsedMs(profileStart));
}

void SceneRenderer::profileSection(const std::string& name, double elapsedMs) {
  if (!profilingEnabled_) return;
  UI_ProfileBucket& bucket = profileBuckets_[name];
  bucket.totalMs += elapsedMs;
  bucket.maxMs = std::max(bucket.maxMs, elapsedMs);
  ++bucket.samples;
}

void SceneRenderer::profileMaybeReport() {
  if (!profilingEnabled_ || profileFrames_ < 120) return;

  std::vector<std::pair<std::string, UI_ProfileBucket>> buckets(profileBuckets_.begin(), profileBuckets_.end());
  std::sort(buckets.begin(), buckets.end(), [](const auto& a, const auto& b) {
    const double avgA = a.second.samples > 0 ? a.second.totalMs / static_cast<double>(a.second.samples) : 0.0;
    const double avgB = b.second.samples > 0 ? b.second.totalMs / static_cast<double>(b.second.samples) : 0.0;
    return avgA > avgB;
  });

  std::ostringstream report;
  report << "\nBlend2DUI profile over " << profileFrames_ << " frames\n";
  for (const auto& [name, bucket] : buckets) {
    if (bucket.samples <= 0) continue;
    const double average = bucket.totalMs / static_cast<double>(bucket.samples);
    report << "  " << name << ": avg " << average << " ms, max " << bucket.maxMs << " ms, samples " << bucket.samples << "\n";
  }
  const std::string text = report.str();
  std::cout << text;
  std::ofstream log(profileLogPath(), std::ios::app);
  log << text;

  profileFrames_ = 0;
  profileBuckets_.clear();
}

}  // namespace Blend2DUI
