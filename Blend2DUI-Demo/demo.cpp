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

  const auto start = Clock::now();
  bool running = true;
  while (running) {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
      running = app.handleEvent(event);
    }

    const auto now = Clock::now();
    const double seconds = std::chrono::duration<double>(now - start).count();
    if (screen.renderFrame(app, seconds)) {
      app.present();
    }
  }

  return 0;
}
