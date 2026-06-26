#pragma once

#include <blend2d/blend2d.h>

#include <memory>

namespace Blend2DUI {

class SceneRenderer;

class Canvas3D {
 public:
  Canvas3D();
  ~Canvas3D();

  Canvas3D(const Canvas3D&) = delete;
  Canvas3D& operator=(const Canvas3D&) = delete;

  void render(SceneRenderer& renderer, const BLRect& rect, double seconds);

 private:
  friend class SceneRenderer;

  struct Impl;

  void renderGL(SceneRenderer& renderer, const BLRect& rect, double seconds);
  void releaseGLResources();

  std::unique_ptr<Impl> impl_;
};

}  // namespace Blend2DUI
