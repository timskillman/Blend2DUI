#include "DemoScreen.h"

#include <SDL3/SDL.h>

#include <chrono>

#ifndef BLEND2DUI_DEMO_ASSET_BASE_PATH
#define BLEND2DUI_DEMO_ASSET_BASE_PATH "."
#endif

int main(int, char**) {
  using Clock = std::chrono::steady_clock;

  Blend2DUI::SceneRenderer app;
  Blend2DUI::DemoScreen screen;
  app.setAssetBasePath(BLEND2DUI_DEMO_ASSET_BASE_PATH);
  if (!app.initialize("Blend2D UI SDL3 Demo", 960, 640)) {
    return 1;
  }

  const auto targetFrameDuration = std::chrono::duration_cast<Clock::duration>(
      std::chrono::duration<double>(1.0 / (app.targetFrameRate() > 1.0 ? app.targetFrameRate() : 60.0)));
  const auto start = Clock::now();
  auto nextFrame = start;
  bool running = true;
  while (running) {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
      running = app.handleEvent(event);
    }

    const auto now = Clock::now();
    if (now < nextFrame) {
      const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(nextFrame - now);
      if (remaining.count() > 1) {
        SDL_Delay(static_cast<Uint32>(remaining.count() - 1));
      } else {
        SDL_Delay(1);
      }
      continue;
    }

    const double seconds = std::chrono::duration<double>(now - start).count();
    if (screen.renderFrame(app, seconds)) {
      app.present();
    }
    const auto afterRender = Clock::now();
    nextFrame += targetFrameDuration;
    if (nextFrame < afterRender) {
      nextFrame = afterRender + targetFrameDuration;
    }
  }

  return 0;
}
