#include "Canvas3D.h"

#include "SceneRenderer.h"

#include <SDL3/SDL_opengles2.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <iostream>
#include <string_view>

namespace Blend2DUI {
namespace {

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
using DisableProc = decltype(&glDisable);
using DrawElementsProc = decltype(&glDrawElements);
using EnableProc = decltype(&glEnable);
using EnableVertexAttribArrayProc = decltype(&glEnableVertexAttribArray);
using GenBuffersProc = decltype(&glGenBuffers);
using GetProgramInfoLogProc = decltype(&glGetProgramInfoLog);
using GetProgramivProc = decltype(&glGetProgramiv);
using GetShaderInfoLogProc = decltype(&glGetShaderInfoLog);
using GetShaderivProc = decltype(&glGetShaderiv);
using GetUniformLocationProc = decltype(&glGetUniformLocation);
using LinkProgramProc = decltype(&glLinkProgram);
using ScissorProc = decltype(&glScissor);
using ShaderSourceProc = decltype(&glShaderSource);
using Uniform3fProc = decltype(&glUniform3f);
using UniformMatrix4fvProc = decltype(&glUniformMatrix4fv);
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

struct GLFunctions {
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
  DisableProc disable = nullptr;
  DrawElementsProc drawElements = nullptr;
  EnableProc enable = nullptr;
  EnableVertexAttribArrayProc enableVertexAttribArray = nullptr;
  GenBuffersProc genBuffers = nullptr;
  GetProgramInfoLogProc getProgramInfoLog = nullptr;
  GetProgramivProc getProgramiv = nullptr;
  GetShaderInfoLogProc getShaderInfoLog = nullptr;
  GetShaderivProc getShaderiv = nullptr;
  GetUniformLocationProc getUniformLocation = nullptr;
  LinkProgramProc linkProgram = nullptr;
  ScissorProc scissor = nullptr;
  ShaderSourceProc shaderSource = nullptr;
  Uniform3fProc uniform3f = nullptr;
  UniformMatrix4fvProc uniformMatrix4fv = nullptr;
  UseProgramProc useProgram = nullptr;
  VertexAttribPointerProc vertexAttribPointer = nullptr;
  ViewportProc viewport = nullptr;

  bool load() {
    return loadProc(attachShader, "glAttachShader") &&
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
           loadProc(disable, "glDisable") &&
           loadProc(drawElements, "glDrawElements") &&
           loadProc(enable, "glEnable") &&
           loadProc(enableVertexAttribArray, "glEnableVertexAttribArray") &&
           loadProc(genBuffers, "glGenBuffers") &&
           loadProc(getProgramInfoLog, "glGetProgramInfoLog") &&
           loadProc(getProgramiv, "glGetProgramiv") &&
           loadProc(getShaderInfoLog, "glGetShaderInfoLog") &&
           loadProc(getShaderiv, "glGetShaderiv") &&
           loadProc(getUniformLocation, "glGetUniformLocation") &&
           loadProc(linkProgram, "glLinkProgram") &&
           loadProc(scissor, "glScissor") &&
           loadProc(shaderSource, "glShaderSource") &&
           loadProc(uniform3f, "glUniform3f") &&
           loadProc(uniformMatrix4fv, "glUniformMatrix4fv") &&
           loadProc(useProgram, "glUseProgram") &&
           loadProc(vertexAttribPointer, "glVertexAttribPointer") &&
           loadProc(viewport, "glViewport");
  }
};

struct Mat4 {
  std::array<GLfloat, 16> m{};
};

Mat4 identityMatrix() {
  Mat4 result{};
  result.m[0] = 1.0f;
  result.m[5] = 1.0f;
  result.m[10] = 1.0f;
  result.m[15] = 1.0f;
  return result;
}

Mat4 multiply(const Mat4& lhs, const Mat4& rhs) {
  Mat4 result{};
  for (int column = 0; column < 4; ++column) {
    for (int row = 0; row < 4; ++row) {
      GLfloat sum = 0.0f;
      for (int index = 0; index < 4; ++index) {
        sum += lhs.m[index * 4 + row] * rhs.m[column * 4 + index];
      }
      result.m[column * 4 + row] = sum;
    }
  }
  return result;
}

Mat4 perspective(float fovYRadians, float aspect, float nearPlane, float farPlane) {
  Mat4 result{};
  const float invTan = 1.0f / std::tan(fovYRadians * 0.5f);
  result.m[0] = invTan / aspect;
  result.m[5] = invTan;
  result.m[10] = (farPlane + nearPlane) / (nearPlane - farPlane);
  result.m[11] = -1.0f;
  result.m[14] = (2.0f * farPlane * nearPlane) / (nearPlane - farPlane);
  return result;
}

Mat4 translation(float x, float y, float z) {
  Mat4 result = identityMatrix();
  result.m[12] = x;
  result.m[13] = y;
  result.m[14] = z;
  return result;
}

Mat4 rotationX(float radians) {
  Mat4 result = identityMatrix();
  const float cosine = std::cos(radians);
  const float sine = std::sin(radians);
  result.m[5] = cosine;
  result.m[6] = sine;
  result.m[9] = -sine;
  result.m[10] = cosine;
  return result;
}

Mat4 rotationY(float radians) {
  Mat4 result = identityMatrix();
  const float cosine = std::cos(radians);
  const float sine = std::sin(radians);
  result.m[0] = cosine;
  result.m[2] = -sine;
  result.m[8] = sine;
  result.m[10] = cosine;
  return result;
}

GLuint compileShader(GLFunctions& gl, GLenum type, std::string_view source) {
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
  std::cerr << "Canvas3D shader compilation failed: " << logBuffer << "\n";
  gl.deleteShader(shader);
  return 0;
}

struct AttributeBinding {
  GLuint location = 0;
  const char* name = nullptr;
};

GLuint createProgram(GLFunctions& gl,
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
  std::cerr << "Canvas3D shader link failed: " << logBuffer << "\n";
  gl.deleteProgram(program);
  return 0;
}

struct Vertex {
  GLfloat position[3];
  GLfloat normal[3];
  GLfloat colour[3];
};

constexpr std::array<Vertex, 24> kVertices = {{
    {{-0.95f, -0.60f, 0.45f}, {0.0f, 0.0f, 1.0f}, {0.97f, 0.57f, 0.37f}},
    {{0.95f, -0.60f, 0.45f}, {0.0f, 0.0f, 1.0f}, {0.97f, 0.57f, 0.37f}},
    {{0.95f, 0.60f, 0.45f}, {0.0f, 0.0f, 1.0f}, {0.97f, 0.57f, 0.37f}},
    {{-0.95f, 0.60f, 0.45f}, {0.0f, 0.0f, 1.0f}, {0.97f, 0.57f, 0.37f}},

    {{-0.95f, -0.60f, -0.45f}, {0.0f, 0.0f, -1.0f}, {0.16f, 0.73f, 0.93f}},
    {{-0.95f, 0.60f, -0.45f}, {0.0f, 0.0f, -1.0f}, {0.16f, 0.73f, 0.93f}},
    {{0.95f, 0.60f, -0.45f}, {0.0f, 0.0f, -1.0f}, {0.16f, 0.73f, 0.93f}},
    {{0.95f, -0.60f, -0.45f}, {0.0f, 0.0f, -1.0f}, {0.16f, 0.73f, 0.93f}},

    {{-0.95f, 0.60f, -0.45f}, {0.0f, 1.0f, 0.0f}, {0.98f, 0.83f, 0.33f}},
    {{-0.95f, 0.60f, 0.45f}, {0.0f, 1.0f, 0.0f}, {0.98f, 0.83f, 0.33f}},
    {{0.95f, 0.60f, 0.45f}, {0.0f, 1.0f, 0.0f}, {0.98f, 0.83f, 0.33f}},
    {{0.95f, 0.60f, -0.45f}, {0.0f, 1.0f, 0.0f}, {0.98f, 0.83f, 0.33f}},

    {{-0.95f, -0.60f, -0.45f}, {0.0f, -1.0f, 0.0f}, {0.33f, 0.87f, 0.57f}},
    {{0.95f, -0.60f, -0.45f}, {0.0f, -1.0f, 0.0f}, {0.33f, 0.87f, 0.57f}},
    {{0.95f, -0.60f, 0.45f}, {0.0f, -1.0f, 0.0f}, {0.33f, 0.87f, 0.57f}},
    {{-0.95f, -0.60f, 0.45f}, {0.0f, -1.0f, 0.0f}, {0.33f, 0.87f, 0.57f}},

    {{0.95f, -0.60f, -0.45f}, {1.0f, 0.0f, 0.0f}, {0.90f, 0.45f, 0.80f}},
    {{0.95f, 0.60f, -0.45f}, {1.0f, 0.0f, 0.0f}, {0.90f, 0.45f, 0.80f}},
    {{0.95f, 0.60f, 0.45f}, {1.0f, 0.0f, 0.0f}, {0.90f, 0.45f, 0.80f}},
    {{0.95f, -0.60f, 0.45f}, {1.0f, 0.0f, 0.0f}, {0.90f, 0.45f, 0.80f}},

    {{-0.95f, -0.60f, -0.45f}, {-1.0f, 0.0f, 0.0f}, {0.50f, 0.58f, 0.97f}},
    {{-0.95f, -0.60f, 0.45f}, {-1.0f, 0.0f, 0.0f}, {0.50f, 0.58f, 0.97f}},
    {{-0.95f, 0.60f, 0.45f}, {-1.0f, 0.0f, 0.0f}, {0.50f, 0.58f, 0.97f}},
    {{-0.95f, 0.60f, -0.45f}, {-1.0f, 0.0f, 0.0f}, {0.50f, 0.58f, 0.97f}},
}};

constexpr std::array<GLushort, 36> kIndices = {{
    0, 1, 2, 0, 2, 3,
    4, 5, 6, 4, 6, 7,
    8, 9, 10, 8, 10, 11,
    12, 13, 14, 12, 14, 15,
    16, 17, 18, 16, 18, 19,
    20, 21, 22, 20, 22, 23,
}};

constexpr AttributeBinding kAttributeBindings[] = {
    {0, "aPosition"},
    {1, "aNormal"},
    {2, "aColour"},
};

constexpr std::string_view kVertexShaderSourceEs3 = R"(#version 300 es
precision mediump float;

layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec3 aColour;

uniform mat4 uMvp;
uniform mat4 uModel;

out vec3 vNormal;
out vec3 vColour;

void main() {
  vNormal = (uModel * vec4(aNormal, 0.0)).xyz;
  vColour = aColour;
  gl_Position = uMvp * vec4(aPosition, 1.0);
}
)";

constexpr std::string_view kFragmentShaderSourceEs3 = R"(#version 300 es
precision mediump float;

in vec3 vNormal;
in vec3 vColour;

uniform vec3 uLightDirection;

out vec4 fragColour;

void main() {
  vec3 normal = normalize(vNormal);
  vec3 light = normalize(-uLightDirection);
  float diffuse = max(dot(normal, light), 0.0);
  float ambient = 0.28;
  float highlight = pow(max(dot(reflect(uLightDirection, normal), vec3(0.0, 0.0, 1.0)), 0.0), 18.0) * 0.18;
  vec3 colour = vColour * (ambient + diffuse * 0.72) + vec3(highlight);
  fragColour = vec4(colour, 1.0);
}
)";

constexpr std::string_view kVertexShaderSourceEs2 = R"(precision mediump float;

attribute vec3 aPosition;
attribute vec3 aNormal;
attribute vec3 aColour;

uniform mat4 uMvp;
uniform mat4 uModel;

varying vec3 vNormal;
varying vec3 vColour;

void main() {
  vNormal = (uModel * vec4(aNormal, 0.0)).xyz;
  vColour = aColour;
  gl_Position = uMvp * vec4(aPosition, 1.0);
}
)";

constexpr std::string_view kFragmentShaderSourceEs2 = R"(precision mediump float;

varying vec3 vNormal;
varying vec3 vColour;

uniform vec3 uLightDirection;

void main() {
  vec3 normal = normalize(vNormal);
  vec3 light = normalize(-uLightDirection);
  float diffuse = max(dot(normal, light), 0.0);
  float ambient = 0.28;
  float highlight = pow(max(dot(reflect(uLightDirection, normal), vec3(0.0, 0.0, 1.0)), 0.0), 18.0) * 0.18;
  vec3 colour = vColour * (ambient + diffuse * 0.72) + vec3(highlight);
  gl_FragColor = vec4(colour, 1.0);
}
)";

}  // namespace

struct Canvas3D::Impl {
  GLFunctions gl;
  bool glLoaded = false;
  GLuint program = 0;
  GLuint vertexBuffer = 0;
  GLuint indexBuffer = 0;
  GLint mvpUniform = -1;
  GLint modelUniform = -1;
  GLint lightDirectionUniform = -1;
};

Canvas3D::Canvas3D()
    : impl_(std::make_unique<Impl>()) {}

Canvas3D::~Canvas3D() {
  releaseGLResources();
}

void Canvas3D::render(SceneRenderer& renderer, const BLRect& rect, double seconds) {
  renderer.queueCanvas3D(*this, rect, seconds);
}

void Canvas3D::releaseGLResources() {
  if (!impl_ || !impl_->glLoaded || !SDL_GL_GetCurrentContext()) return;

  if (impl_->indexBuffer != 0) {
    impl_->gl.deleteBuffers(1, &impl_->indexBuffer);
    impl_->indexBuffer = 0;
  }
  if (impl_->vertexBuffer != 0) {
    impl_->gl.deleteBuffers(1, &impl_->vertexBuffer);
    impl_->vertexBuffer = 0;
  }
  if (impl_->program != 0) {
    impl_->gl.deleteProgram(impl_->program);
    impl_->program = 0;
  }
}

void Canvas3D::renderGL(SceneRenderer& renderer, const BLRect& rect, double seconds) {
  if (!impl_) return;
  if (rect.w <= 4.0 || rect.h <= 4.0) return;

  if (!impl_->glLoaded) {
    impl_->glLoaded = impl_->gl.load();
    if (!impl_->glLoaded) return;
  }

  if (impl_->program == 0) {
    const bool useEs3Shaders = renderer.glMajorVersion_ >= 3;
    const std::string_view vertexShaderSource = useEs3Shaders ? kVertexShaderSourceEs3 : kVertexShaderSourceEs2;
    const std::string_view fragmentShaderSource = useEs3Shaders ? kFragmentShaderSourceEs3 : kFragmentShaderSourceEs2;
    const AttributeBinding* bindings = useEs3Shaders ? nullptr : kAttributeBindings;
    const size_t bindingCount = useEs3Shaders ? 0 : std::size(kAttributeBindings);
    impl_->program = createProgram(impl_->gl,
                                   vertexShaderSource,
                                   fragmentShaderSource,
                                   bindings,
                                   bindingCount);
    if (impl_->program == 0) return;

    impl_->mvpUniform = impl_->gl.getUniformLocation(impl_->program, "uMvp");
    impl_->modelUniform = impl_->gl.getUniformLocation(impl_->program, "uModel");
    impl_->lightDirectionUniform = impl_->gl.getUniformLocation(impl_->program, "uLightDirection");

    impl_->gl.genBuffers(1, &impl_->vertexBuffer);
    impl_->gl.genBuffers(1, &impl_->indexBuffer);
    if (impl_->vertexBuffer == 0 || impl_->indexBuffer == 0) {
      std::cerr << "Canvas3D buffer creation failed\n";
      releaseGLResources();
      return;
    }

    impl_->gl.bindBuffer(GL_ARRAY_BUFFER, impl_->vertexBuffer);
    impl_->gl.bufferData(GL_ARRAY_BUFFER,
                         static_cast<GLsizeiptr>(sizeof(kVertices)),
                         kVertices.data(),
                         GL_STATIC_DRAW);

    impl_->gl.bindBuffer(GL_ELEMENT_ARRAY_BUFFER, impl_->indexBuffer);
    impl_->gl.bufferData(GL_ELEMENT_ARRAY_BUFFER,
                         static_cast<GLsizeiptr>(sizeof(kIndices)),
                         kIndices.data(),
                         GL_STATIC_DRAW);

    impl_->gl.bindBuffer(GL_ARRAY_BUFFER, 0);
    impl_->gl.bindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
  }

  const GLint pixelX = std::max<GLint>(0, static_cast<GLint>(std::lround(rect.x)));
  const GLint pixelYTop = std::max<GLint>(0, static_cast<GLint>(std::lround(rect.y)));
  const GLsizei pixelWidth = std::max<GLsizei>(1, static_cast<GLsizei>(std::lround(rect.w)));
  const GLsizei pixelHeight = std::max<GLsizei>(1, static_cast<GLsizei>(std::lround(rect.h)));
  const GLint pixelY = std::max<GLint>(0, renderer.height() - pixelYTop - pixelHeight);

  impl_->gl.enable(GL_SCISSOR_TEST);
  impl_->gl.scissor(pixelX, pixelY, pixelWidth, pixelHeight);
  impl_->gl.viewport(pixelX, pixelY, pixelWidth, pixelHeight);
  impl_->gl.enable(GL_DEPTH_TEST);
  impl_->gl.disable(GL_BLEND);

  impl_->gl.clearColor(0.07f, 0.10f, 0.14f, 1.0f);
  impl_->gl.clear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

  const float aspect = static_cast<float>(pixelWidth) / static_cast<float>(pixelHeight);
  const Mat4 projection = perspective(0.85f, std::max(0.35f, aspect), 0.1f, 20.0f);
  const Mat4 view = translation(0.0f, 0.0f, -3.6f);
  const Mat4 model = multiply(rotationY(static_cast<float>(seconds * 1.2)),
                              rotationX(static_cast<float>(seconds * 0.75 + 0.35)));
  const Mat4 mvp = multiply(projection, multiply(view, model));

  impl_->gl.useProgram(impl_->program);
  impl_->gl.uniformMatrix4fv(impl_->mvpUniform, 1, GL_FALSE, mvp.m.data());
  impl_->gl.uniformMatrix4fv(impl_->modelUniform, 1, GL_FALSE, model.m.data());
  impl_->gl.uniform3f(impl_->lightDirectionUniform, -0.4f, 0.7f, 0.6f);

  impl_->gl.bindBuffer(GL_ARRAY_BUFFER, impl_->vertexBuffer);
  impl_->gl.bindBuffer(GL_ELEMENT_ARRAY_BUFFER, impl_->indexBuffer);

  impl_->gl.enableVertexAttribArray(0);
  impl_->gl.enableVertexAttribArray(1);
  impl_->gl.enableVertexAttribArray(2);
  impl_->gl.vertexAttribPointer(0,
                                3,
                                GL_FLOAT,
                                GL_FALSE,
                                static_cast<GLsizei>(sizeof(Vertex)),
                                reinterpret_cast<const void*>(offsetof(Vertex, position)));
  impl_->gl.vertexAttribPointer(1,
                                3,
                                GL_FLOAT,
                                GL_FALSE,
                                static_cast<GLsizei>(sizeof(Vertex)),
                                reinterpret_cast<const void*>(offsetof(Vertex, normal)));
  impl_->gl.vertexAttribPointer(2,
                                3,
                                GL_FLOAT,
                                GL_FALSE,
                                static_cast<GLsizei>(sizeof(Vertex)),
                                reinterpret_cast<const void*>(offsetof(Vertex, colour)));

  impl_->gl.drawElements(GL_TRIANGLES,
                         static_cast<GLsizei>(kIndices.size()),
                         GL_UNSIGNED_SHORT,
                         nullptr);

  impl_->gl.bindBuffer(GL_ARRAY_BUFFER, 0);
  impl_->gl.bindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
  impl_->gl.useProgram(0);
  impl_->gl.disable(GL_DEPTH_TEST);
  impl_->gl.disable(GL_SCISSOR_TEST);
}

}  // namespace Blend2DUI
