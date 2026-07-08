#include "SvgEditor/SvgEditorApp.h"

#include <SDL3/SDL.h>

#include <chrono>
#include <filesystem>

int main(int argc, char** argv) {
  using Clock = std::chrono::steady_clock;

  Blend2DUI::SceneRenderer renderer;
  SvgEditor::SvgEditorApp app;
  std::error_code pathError;
  std::filesystem::path assetBasePath = std::filesystem::current_path(pathError);
  if (argc > 0 && argv != nullptr && argv[0] != nullptr && argv[0][0] != '\0') {
    const std::filesystem::path executablePath = std::filesystem::absolute(argv[0], pathError);
    if (!pathError) {
      assetBasePath = executablePath.parent_path();
    }
  }
  renderer.setAssetBasePath(assetBasePath.string());

  if (!renderer.initialize("Blend2DUI SVG Editor", 1400, 920)) {
    return 1;
  }

  const auto start = Clock::now();
  bool running = true;
  while (running) {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
      running = renderer.handleEvent(event);
    }

    const auto now = Clock::now();
    const double seconds = std::chrono::duration<double>(now - start).count();
    if (app.renderFrame(renderer, seconds)) {
      renderer.present();
    }
  }

  return 0;
}
