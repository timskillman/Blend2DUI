#include "Blend2DUI/SdlBlend2DRenderer.h"

#include <SDL3/SDL.h>

#include <chrono>

int main(int, char**) {
  Blend2DUI::SdlBlend2DRenderer app;
  if (!app.initialize("Blend2D UI SDL3 Demo", 960, 640)) {
    return 1;
  }

  const auto start = std::chrono::steady_clock::now();
  bool running = true;
  while (running) {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
      running = app.handleEvent(event);
    }

    const auto now = std::chrono::steady_clock::now();
    const double seconds = std::chrono::duration<double>(now - start).count();
    if (app.renderDemoFrame(seconds)) {
      app.present();
    }

    SDL_Delay(16);
  }

  return 0;
}
