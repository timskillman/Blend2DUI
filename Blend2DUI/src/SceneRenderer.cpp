#include "SceneRenderer.h"

#include "Canvas3D.h"

#include <SDL3/SDL_opengles2.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string_view>
#include <utility>
#include <vector>

namespace Blend2DUI {
namespace {

using Clock = std::chrono::steady_clock;

using ActiveTextureProc = decltype(&glActiveTexture);
using AttachShaderProc = decltype(&glAttachShader);
using BindAttribLocationProc = decltype(&glBindAttribLocation);
using BindBufferProc = decltype(&glBindBuffer);
using BindTextureProc = decltype(&glBindTexture);
using BufferDataProc = decltype(&glBufferData);
using ClearProc = decltype(&glClear);
using ClearColorProc = decltype(&glClearColor);
using CompileShaderProc = decltype(&glCompileShader);
using CreateProgramProc = decltype(&glCreateProgram);
using CreateShaderProc = decltype(&glCreateShader);
using DeleteBuffersProc = decltype(&glDeleteBuffers);
using DeleteProgramProc = decltype(&glDeleteProgram);
using DeleteShaderProc = decltype(&glDeleteShader);
using DeleteTexturesProc = decltype(&glDeleteTextures);
using DisableProc = decltype(&glDisable);
using DrawArraysProc = decltype(&glDrawArrays);
using EnableVertexAttribArrayProc = decltype(&glEnableVertexAttribArray);
using GenBuffersProc = decltype(&glGenBuffers);
using GenTexturesProc = decltype(&glGenTextures);
using GetProgramInfoLogProc = decltype(&glGetProgramInfoLog);
using GetProgramivProc = decltype(&glGetProgramiv);
using GetShaderInfoLogProc = decltype(&glGetShaderInfoLog);
using GetShaderivProc = decltype(&glGetShaderiv);
using GetUniformLocationProc = decltype(&glGetUniformLocation);
using LinkProgramProc = decltype(&glLinkProgram);
using ShaderSourceProc = decltype(&glShaderSource);
using TexImage2DProc = decltype(&glTexImage2D);
using TexParameteriProc = decltype(&glTexParameteri);
using TexSubImage2DProc = decltype(&glTexSubImage2D);
using Uniform1iProc = decltype(&glUniform1i);
using UseProgramProc = decltype(&glUseProgram);
using VertexAttribPointerProc = decltype(&glVertexAttribPointer);
using ViewportProc = decltype(&glViewport);

template <typename Proc>
bool loadProc(Proc& proc, const char* name) {
  proc = reinterpret_cast<Proc>(SDL_GL_GetProcAddress(name));
  if (!proc) {
    std::cerr << "SDL_GL_GetProcAddress failed for " << name << ": " << SDL_GetError() << "\n";
    return false;
  }
  return true;
}

struct PresentationGLFunctions {
  ActiveTextureProc activeTexture = nullptr;
  AttachShaderProc attachShader = nullptr;
  BindAttribLocationProc bindAttribLocation = nullptr;
  BindBufferProc bindBuffer = nullptr;
  BindTextureProc bindTexture = nullptr;
  BufferDataProc bufferData = nullptr;
  ClearProc clear = nullptr;
  ClearColorProc clearColor = nullptr;
  CompileShaderProc compileShader = nullptr;
  CreateProgramProc createProgram = nullptr;
  CreateShaderProc createShader = nullptr;
  DeleteBuffersProc deleteBuffers = nullptr;
  DeleteProgramProc deleteProgram = nullptr;
  DeleteShaderProc deleteShader = nullptr;
  DeleteTexturesProc deleteTextures = nullptr;
  DisableProc disable = nullptr;
  DrawArraysProc drawArrays = nullptr;
  EnableVertexAttribArrayProc enableVertexAttribArray = nullptr;
  GenBuffersProc genBuffers = nullptr;
  GenTexturesProc genTextures = nullptr;
  GetProgramInfoLogProc getProgramInfoLog = nullptr;
  GetProgramivProc getProgramiv = nullptr;
  GetShaderInfoLogProc getShaderInfoLog = nullptr;
  GetShaderivProc getShaderiv = nullptr;
  GetUniformLocationProc getUniformLocation = nullptr;
  LinkProgramProc linkProgram = nullptr;
  ShaderSourceProc shaderSource = nullptr;
  TexImage2DProc texImage2D = nullptr;
  TexParameteriProc texParameteri = nullptr;
  TexSubImage2DProc texSubImage2D = nullptr;
  Uniform1iProc uniform1i = nullptr;
  UseProgramProc useProgram = nullptr;
  VertexAttribPointerProc vertexAttribPointer = nullptr;
  ViewportProc viewport = nullptr;

  bool load() {
    return loadProc(activeTexture, "glActiveTexture") &&
           loadProc(attachShader, "glAttachShader") &&
           loadProc(bindAttribLocation, "glBindAttribLocation") &&
           loadProc(bindBuffer, "glBindBuffer") &&
           loadProc(bindTexture, "glBindTexture") &&
           loadProc(bufferData, "glBufferData") &&
           loadProc(clear, "glClear") &&
           loadProc(clearColor, "glClearColor") &&
           loadProc(compileShader, "glCompileShader") &&
           loadProc(createProgram, "glCreateProgram") &&
           loadProc(createShader, "glCreateShader") &&
           loadProc(deleteBuffers, "glDeleteBuffers") &&
           loadProc(deleteProgram, "glDeleteProgram") &&
           loadProc(deleteShader, "glDeleteShader") &&
           loadProc(deleteTextures, "glDeleteTextures") &&
           loadProc(disable, "glDisable") &&
           loadProc(drawArrays, "glDrawArrays") &&
           loadProc(enableVertexAttribArray, "glEnableVertexAttribArray") &&
           loadProc(genBuffers, "glGenBuffers") &&
           loadProc(genTextures, "glGenTextures") &&
           loadProc(getProgramInfoLog, "glGetProgramInfoLog") &&
           loadProc(getProgramiv, "glGetProgramiv") &&
           loadProc(getShaderInfoLog, "glGetShaderInfoLog") &&
           loadProc(getShaderiv, "glGetShaderiv") &&
           loadProc(getUniformLocation, "glGetUniformLocation") &&
           loadProc(linkProgram, "glLinkProgram") &&
           loadProc(shaderSource, "glShaderSource") &&
           loadProc(texImage2D, "glTexImage2D") &&
           loadProc(texParameteri, "glTexParameteri") &&
           loadProc(texSubImage2D, "glTexSubImage2D") &&
           loadProc(uniform1i, "glUniform1i") &&
           loadProc(useProgram, "glUseProgram") &&
           loadProc(vertexAttribPointer, "glVertexAttribPointer") &&
           loadProc(viewport, "glViewport");
  }
};

PresentationGLFunctions gPresentationGL;
bool gPresentationGLLoaded = false;

double elapsedMs(Clock::time_point start) {
  return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}

bool profileEnvironmentEnabled() {
  const char* value = std::getenv("BLEND2DUI_PROFILE");
  if (!value) return false;
  const std::string text(value);
  return text == "1" || text == "true" || text == "TRUE" || text == "on" || text == "ON";
}

bool parseEnvironmentBool(const char* name, bool fallback) {
  const char* value = std::getenv(name);
  if (!value) return fallback;
  const std::string text(value);
  if (text == "1" || text == "true" || text == "TRUE" || text == "on" || text == "ON") return true;
  if (text == "0" || text == "false" || text == "FALSE" || text == "off" || text == "OFF") return false;
  return fallback;
}

bool defaultLowPowerMode() {
#if defined(__linux__) && (defined(__arm__) || defined(__aarch64__))
  return true;
#else
  return false;
#endif
}

bool detectLowPowerMode() {
  return parseEnvironmentBool("BLEND2DUI_LOW_POWER", defaultLowPowerMode());
}

double detectTargetFrameRate(bool lowPowerMode) {
  const double fallback = lowPowerMode ? 30.0 : 60.0;
  const char* value = std::getenv("BLEND2DUI_TARGET_FPS");
  if (!value || value[0] == '\0') return fallback;

  char* parsedEnd = nullptr;
  const double parsed = std::strtod(value, &parsedEnd);
  if (parsedEnd == value || parsed < 5.0 || parsed > 240.0) return fallback;
  return parsed;
}

const char* profileLogPath() {
  const char* value = std::getenv("BLEND2DUI_PROFILE_LOG");
  return value && value[0] != '\0' ? value : "blend2d_ui_profile.log";
}

GLuint compileShader(PresentationGLFunctions& gl, GLenum type, std::string_view source) {
  const GLuint shader = gl.createShader(type);
  if (!shader) return 0;

  const GLchar* sourcePtr = source.data();
  const GLint sourceLength = static_cast<GLint>(source.size());
  gl.shaderSource(shader, 1, &sourcePtr, &sourceLength);
  gl.compileShader(shader);

  GLint compileStatus = 0;
  gl.getShaderiv(shader, GL_COMPILE_STATUS, &compileStatus);
  if (compileStatus == GL_TRUE) return shader;

  char logBuffer[1024] = {};
  GLsizei logLength = 0;
  gl.getShaderInfoLog(shader, static_cast<GLsizei>(std::size(logBuffer)), &logLength, logBuffer);
  std::cerr << "Presentation shader compilation failed: " << logBuffer << "\n";
  gl.deleteShader(shader);
  return 0;
}

struct AttributeBinding {
  GLuint location = 0;
  const char* name = nullptr;
};

GLuint createProgram(PresentationGLFunctions& gl,
                     std::string_view vertexSource,
                     std::string_view fragmentSource,
                     const AttributeBinding* bindings = nullptr,
                     size_t bindingCount = 0) {
  const GLuint vertexShader = compileShader(gl, GL_VERTEX_SHADER, vertexSource);
  if (!vertexShader) return 0;

  const GLuint fragmentShader = compileShader(gl, GL_FRAGMENT_SHADER, fragmentSource);
  if (!fragmentShader) {
    gl.deleteShader(vertexShader);
    return 0;
  }

  const GLuint program = gl.createProgram();
  if (!program) {
    gl.deleteShader(vertexShader);
    gl.deleteShader(fragmentShader);
    return 0;
  }

  gl.attachShader(program, vertexShader);
  gl.attachShader(program, fragmentShader);
  for (size_t i = 0; i < bindingCount; ++i) {
    if (bindings[i].name && bindings[i].name[0] != '\0') {
      gl.bindAttribLocation(program, bindings[i].location, bindings[i].name);
    }
  }
  gl.linkProgram(program);

  GLint linkStatus = 0;
  gl.getProgramiv(program, GL_LINK_STATUS, &linkStatus);
  gl.deleteShader(vertexShader);
  gl.deleteShader(fragmentShader);
  if (linkStatus == GL_TRUE) return program;

  char logBuffer[1024] = {};
  GLsizei logLength = 0;
  gl.getProgramInfoLog(program, static_cast<GLsizei>(std::size(logBuffer)), &logLength, logBuffer);
  std::cerr << "Presentation shader link failed: " << logBuffer << "\n";
  gl.deleteProgram(program);
  return 0;
}

struct QuadVertex {
  GLfloat position[2];
  GLfloat uv[2];
};

constexpr std::array<QuadVertex, 4> kPresentationQuad = {{
    {{-1.0f, 1.0f}, {0.0f, 0.0f}},
    {{-1.0f, -1.0f}, {0.0f, 1.0f}},
    {{1.0f, 1.0f}, {1.0f, 0.0f}},
    {{1.0f, -1.0f}, {1.0f, 1.0f}},
}};

constexpr AttributeBinding kPresentationBindings[] = {
    {0, "aPosition"},
    {1, "aUv"},
};

constexpr std::string_view kPresentationVertexShaderEs3 = R"(#version 300 es
precision mediump float;

layout(location = 0) in vec2 aPosition;
layout(location = 1) in vec2 aUv;

out vec2 vUv;

void main() {
  vUv = aUv;
  gl_Position = vec4(aPosition, 0.0, 1.0);
}
)";

constexpr std::string_view kPresentationFragmentShaderEs3 = R"(#version 300 es
precision mediump float;

uniform sampler2D uTexture;

in vec2 vUv;

out vec4 fragColour;

void main() {
  vec4 sampled = texture(uTexture, vUv);
  fragColour = sampled.bgra;
}
)";

constexpr std::string_view kPresentationVertexShaderEs2 = R"(precision mediump float;

attribute vec2 aPosition;
attribute vec2 aUv;

varying vec2 vUv;

void main() {
  vUv = aUv;
  gl_Position = vec4(aPosition, 0.0, 1.0);
}
)";

constexpr std::string_view kPresentationFragmentShaderEs2 = R"(precision mediump float;

uniform sampler2D uTexture;

varying vec2 vUv;

void main() {
  vec4 sampled = texture2D(uTexture, vUv);
  gl_FragColor = sampled.bgra;
}
)";

struct GLSetupCandidate {
  int majorVersion = 0;
  int minorVersion = 0;
  int depthBits = 0;
  int stencilBits = 0;
};

struct WindowSetupCandidate {
  SDL_WindowFlags flags = SDL_WINDOW_OPENGL;
  const char* label = "";
};

constexpr std::array<GLSetupCandidate, 4> kGLSetupCandidates = {{
    {3, 0, 24, 8},
    {3, 0, 16, 0},
    {2, 0, 24, 8},
    {2, 0, 16, 0},
}};

constexpr std::array<WindowSetupCandidate, 2> kWindowSetupCandidates = {{
    {static_cast<SDL_WindowFlags>(SDL_WINDOW_RESIZABLE | SDL_WINDOW_OPENGL), "resizable"},
    {SDL_WINDOW_OPENGL, "fixed-size"},
}};

}  // namespace

SceneRenderer::~SceneRenderer() {
  shutdown();
}

void SceneRenderer::setAssetBasePath(std::string assetBasePath) {
  assetBasePath_ = assetBasePath.empty() ? "." : std::move(assetBasePath);
  buttonResources_.assetBasePath = assetBasePath_;
  imageCache_.clear();
  fontCache_.clear();
  shapedTextCache_.clear();
}

bool SceneRenderer::initialize(const std::string& title, int width, int height) {
  profilingEnabled_ = profileEnvironmentEnabled();
  lowPowerMode_ = detectLowPowerMode();
  targetFrameRate_ = detectTargetFrameRate(lowPowerMode_);
  if (profilingEnabled_) {
    std::ofstream log(profileLogPath(), std::ios::trunc);
    log << "Blend2DUI profiling enabled\n";
  }
  if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
    std::cerr << "SDL_Init failed: " << SDL_GetError() << "\n";
    return false;
  }

  glReady_ = false;
  glMajorVersion_ = 0;
  glMinorVersion_ = 0;

  std::string lastGlError;
  for (const WindowSetupCandidate& windowCandidate : kWindowSetupCandidates) {
    for (const GLSetupCandidate& candidate : kGLSetupCandidates) {
      SDL_GL_ResetAttributes();
      SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
      SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, candidate.majorVersion);
      SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, candidate.minorVersion);
      SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
      SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, candidate.depthBits);
      SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, candidate.stencilBits);

      window_ = SDL_CreateWindow(title.c_str(), width, height, windowCandidate.flags);
      if (!window_) {
        lastGlError = SDL_GetError();
        std::cerr << "SDL_CreateWindow failed for " << windowCandidate.label
                  << " OpenGL ES " << candidate.majorVersion << "." << candidate.minorVersion
                  << " depth=" << candidate.depthBits
                  << " stencil=" << candidate.stencilBits
                  << ": " << lastGlError << "\n";
        continue;
      }

      if (initializeOpenGL()) {
        glMajorVersion_ = candidate.majorVersion;
        glMinorVersion_ = candidate.minorVersion;

        int actualMajorVersion = 0;
        int actualMinorVersion = 0;
        SDL_GL_GetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, &actualMajorVersion);
        SDL_GL_GetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, &actualMinorVersion);
        if (actualMajorVersion > 0) glMajorVersion_ = actualMajorVersion;
        if (actualMinorVersion >= 0) glMinorVersion_ = actualMinorVersion;

        int actualDepthBits = 0;
        int actualStencilBits = 0;
        SDL_GL_GetAttribute(SDL_GL_DEPTH_SIZE, &actualDepthBits);
        SDL_GL_GetAttribute(SDL_GL_STENCIL_SIZE, &actualStencilBits);
        std::cerr << "Using " << windowCandidate.label
                  << " OpenGL ES " << glMajorVersion_ << "." << glMinorVersion_
                  << " depth=" << actualDepthBits
                  << " stencil=" << actualStencilBits << "\n";
        break;
      }

      lastGlError = SDL_GetError();
      if (window_) {
        SDL_DestroyWindow(window_);
        window_ = nullptr;
      }
    }

    if (window_ && glContext_) {
      break;
    }
  }

  if (!window_ || !glContext_) {
    if (!lastGlError.empty()) {
      std::cerr << "Unable to create a compatible OpenGL ES window/context: " << lastGlError << "\n";
    }
    shutdown();
    return false;
  }

  buttonResources_.images = &imageCache_;
  buttonResources_.fonts = &fontCache_;
  buttonResources_.shapedText = &shapedTextCache_;
  buttonResources_.assetBasePath = assetBasePath_;
  buttonResources_.lowPowerMode = lowPowerMode_;
  if (lowPowerMode_) {
    std::cerr << "Blend2DUI low-power mode enabled, target FPS " << targetFrameRate_ << "\n";
  }
  SDL_StartTextInput(window_);

  return ensureBackBuffer() && ensurePresentationResources();
}

bool SceneRenderer::initializeOpenGL() {
  glReady_ = false;
  glContext_ = SDL_GL_CreateContext(window_);
  if (!glContext_) {
    std::cerr << "SDL_GL_CreateContext failed: " << SDL_GetError() << "\n";
    return false;
  }
  if (!SDL_GL_MakeCurrent(window_, glContext_)) {
    std::cerr << "SDL_GL_MakeCurrent failed: " << SDL_GetError() << "\n";
    SDL_GL_DestroyContext(glContext_);
    glContext_ = nullptr;
    return false;
  }
  if (!SDL_GL_SetSwapInterval(1)) {
    std::cerr << "SDL_GL_SetSwapInterval warning: " << SDL_GetError() << "\n";
  }
  glReady_ = true;
  return true;
}

void SceneRenderer::shutdown() {
  if (frameActive_) {
    context_.end();
    frameActive_ = false;
  }
  if (window_ && glContext_) {
    SDL_GL_MakeCurrent(window_, glContext_);
  }
  destroyPresentationResources();
  if (glContext_) {
    SDL_GL_DestroyContext(glContext_);
    glContext_ = nullptr;
  }
  gPresentationGLLoaded = false;
  gPresentationGL = PresentationGLFunctions();
  if (window_) {
    SDL_DestroyWindow(window_);
    window_ = nullptr;
  }
  SDL_Quit();
  width_ = 0;
  height_ = 0;
  glReady_ = false;
  glMajorVersion_ = 0;
  glMinorVersion_ = 0;
  lowPowerMode_ = false;
  targetFrameRate_ = 60.0;
  image_.reset();
  uploadBuffer_.clear();
  canvas3DRequests_.clear();
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

  if (pixelWidth == width_ && pixelHeight == height_) {
    if (profilingEnabled_) profileSection("ensureBackBuffer", elapsedMs(profileStart));
    return true;
  }

  const bool resized = resizeBackBuffer(pixelWidth, pixelHeight);
  if (profilingEnabled_) profileSection("ensureBackBuffer", elapsedMs(profileStart));
  return resized;
}

bool SceneRenderer::resizeBackBuffer(int width, int height) {
  image_.create(width, height, BL_FORMAT_PRGB32);
  width_ = width;
  height_ = height;

  if (!glReady_) return true;
  if (!ensurePresentationResources()) return false;

  SDL_GL_MakeCurrent(window_, glContext_);
  gPresentationGL.bindTexture(GL_TEXTURE_2D, glBackBufferTexture_);
  gPresentationGL.texImage2D(GL_TEXTURE_2D,
                             0,
                             GL_RGBA,
                             width,
                             height,
                             0,
                             GL_RGBA,
                             GL_UNSIGNED_BYTE,
                             nullptr);
  gPresentationGL.bindTexture(GL_TEXTURE_2D, 0);
  return true;
}

bool SceneRenderer::ensurePresentationResources() {
  if (!glReady_) return false;
  SDL_GL_MakeCurrent(window_, glContext_);

  if (!gPresentationGLLoaded) {
    gPresentationGLLoaded = gPresentationGL.load();
    if (!gPresentationGLLoaded) return false;
  }

  if (glPresentationProgram_ == 0) {
    const bool useEs3Shaders = glMajorVersion_ >= 3;
    const std::string_view vertexShaderSource = useEs3Shaders ? kPresentationVertexShaderEs3 : kPresentationVertexShaderEs2;
    const std::string_view fragmentShaderSource = useEs3Shaders ? kPresentationFragmentShaderEs3 : kPresentationFragmentShaderEs2;
    const AttributeBinding* bindings = useEs3Shaders ? nullptr : kPresentationBindings;
    const size_t bindingCount = useEs3Shaders ? 0 : std::size(kPresentationBindings);
    glPresentationProgram_ = createProgram(gPresentationGL,
                                           vertexShaderSource,
                                           fragmentShaderSource,
                                           bindings,
                                           bindingCount);
    if (glPresentationProgram_ == 0) return false;
    glPresentationTextureUniform_ = gPresentationGL.getUniformLocation(glPresentationProgram_, "uTexture");
  }

  if (glPresentationVbo_ == 0) {
    gPresentationGL.genBuffers(1, &glPresentationVbo_);
    if (glPresentationVbo_ == 0) {
      std::cerr << "glGenBuffers failed for presentation quad\n";
      return false;
    }
    gPresentationGL.bindBuffer(GL_ARRAY_BUFFER, glPresentationVbo_);
    gPresentationGL.bufferData(GL_ARRAY_BUFFER,
                               static_cast<GLsizeiptr>(sizeof(kPresentationQuad)),
                               kPresentationQuad.data(),
                               GL_STATIC_DRAW);
    gPresentationGL.bindBuffer(GL_ARRAY_BUFFER, 0);
  }

  if (glBackBufferTexture_ == 0) {
    gPresentationGL.genTextures(1, &glBackBufferTexture_);
    if (glBackBufferTexture_ == 0) {
      std::cerr << "glGenTextures failed for Blend2D backbuffer\n";
      return false;
    }
    const GLint filter = lowPowerMode_ ? GL_NEAREST : GL_LINEAR;
    gPresentationGL.bindTexture(GL_TEXTURE_2D, glBackBufferTexture_);
    gPresentationGL.texParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
    gPresentationGL.texParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
    gPresentationGL.texParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    gPresentationGL.texParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    gPresentationGL.texImage2D(GL_TEXTURE_2D,
                               0,
                               GL_RGBA,
                               std::max(1, width_),
                               std::max(1, height_),
                               0,
                               GL_RGBA,
                               GL_UNSIGNED_BYTE,
                               nullptr);
    gPresentationGL.bindTexture(GL_TEXTURE_2D, 0);
  }

  return true;
}

void SceneRenderer::destroyPresentationResources() {
  if (!gPresentationGLLoaded) return;

  if (glPresentationVbo_ != 0) {
    gPresentationGL.deleteBuffers(1, &glPresentationVbo_);
    glPresentationVbo_ = 0;
  }
  if (glBackBufferTexture_ != 0) {
    gPresentationGL.deleteTextures(1, &glBackBufferTexture_);
    glBackBufferTexture_ = 0;
  }
  if (glPresentationProgram_ != 0) {
    gPresentationGL.deleteProgram(glPresentationProgram_);
    glPresentationProgram_ = 0;
  }
  glPresentationTextureUniform_ = -1;
}

bool SceneRenderer::beginFrame(double seconds) {
  const auto profileStart = Clock::now();
  if (!ensureBackBuffer()) return false;
  if (frameActive_) {
    context_.end();
    frameActive_ = false;
  }

  canvas3DRequests_.clear();
  frameSeconds_ = seconds;
  modalOverlayActive_ = false;
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
  if (!ensurePresentationResources()) return false;

  BLImageData data;
  if (image_.get_data(&data) != BL_SUCCESS) {
    std::cerr << "BLImage::get_data failed\n";
    return false;
  }

  const std::size_t expectedStride = static_cast<std::size_t>(width_) * 4u;
  const std::byte* sourcePixels = static_cast<const std::byte*>(data.pixel_data);
  const void* uploadPixels = data.pixel_data;

  if (static_cast<std::size_t>(data.stride) != expectedStride) {
    uploadBuffer_.resize(expectedStride * static_cast<std::size_t>(height_));
    for (int row = 0; row < height_; ++row) {
      std::memcpy(uploadBuffer_.data() + expectedStride * static_cast<std::size_t>(row),
                  sourcePixels + static_cast<std::size_t>(data.stride) * static_cast<std::size_t>(row),
                  expectedStride);
    }
    uploadPixels = uploadBuffer_.data();
  }

  SDL_GL_MakeCurrent(window_, glContext_);
  gPresentationGL.bindTexture(GL_TEXTURE_2D, glBackBufferTexture_);
  gPresentationGL.texSubImage2D(GL_TEXTURE_2D,
                                0,
                                0,
                                0,
                                width_,
                                height_,
                                GL_RGBA,
                                GL_UNSIGNED_BYTE,
                                uploadPixels);
  gPresentationGL.bindTexture(GL_TEXTURE_2D, 0);

  if (profilingEnabled_) profileSection("uploadTexture", elapsedMs(profileStart));
  return true;
}

void SceneRenderer::present() {
  const auto profileStart = Clock::now();
  if (!window_ || !glContext_ || glBackBufferTexture_ == 0 || glPresentationProgram_ == 0) return;

  SDL_GL_MakeCurrent(window_, glContext_);

  gPresentationGL.viewport(0, 0, width_, height_);
  gPresentationGL.clearColor(0.07f, 0.09f, 0.15f, 1.0f);
  gPresentationGL.clear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  gPresentationGL.disable(GL_DEPTH_TEST);
  gPresentationGL.disable(GL_SCISSOR_TEST);
  gPresentationGL.disable(GL_BLEND);

  gPresentationGL.useProgram(glPresentationProgram_);
  gPresentationGL.activeTexture(GL_TEXTURE0);
  gPresentationGL.bindTexture(GL_TEXTURE_2D, glBackBufferTexture_);
  gPresentationGL.uniform1i(glPresentationTextureUniform_, 0);
  gPresentationGL.bindBuffer(GL_ARRAY_BUFFER, glPresentationVbo_);
  gPresentationGL.enableVertexAttribArray(0);
  gPresentationGL.enableVertexAttribArray(1);
  gPresentationGL.vertexAttribPointer(0,
                                      2,
                                      GL_FLOAT,
                                      GL_FALSE,
                                      static_cast<GLsizei>(sizeof(QuadVertex)),
                                      reinterpret_cast<const void*>(offsetof(QuadVertex, position)));
  gPresentationGL.vertexAttribPointer(1,
                                      2,
                                      GL_FLOAT,
                                      GL_FALSE,
                                      static_cast<GLsizei>(sizeof(QuadVertex)),
                                      reinterpret_cast<const void*>(offsetof(QuadVertex, uv)));
  gPresentationGL.drawArrays(GL_TRIANGLE_STRIP, 0, static_cast<GLsizei>(kPresentationQuad.size()));
  gPresentationGL.bindBuffer(GL_ARRAY_BUFFER, 0);
  gPresentationGL.bindTexture(GL_TEXTURE_2D, 0);
  gPresentationGL.useProgram(0);

  if (!modalOverlayActive_) {
    for (const Canvas3DRequest& request : canvas3DRequests_) {
      if (request.canvas) {
        request.canvas->renderGL(*this, request.rect, request.seconds);
      }
    }
  }
  canvas3DRequests_.clear();

  SDL_GL_SwapWindow(window_);
  if (profilingEnabled_) profileSection("present", elapsedMs(profileStart));
}

void SceneRenderer::queueCanvas3D(Canvas3D& canvas, const BLRect& rect, double seconds) {
  canvas3DRequests_.push_back(Canvas3DRequest{&canvas, rect, seconds});
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
