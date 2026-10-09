// SPDX-License-Identifier: MIT
// Both measurements use the very same linked program, VAO and GL context.
#include "../src/opengl/gl_api.h"
#define SDL_MAIN_HANDLED
#include <SDL.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <stdexcept>

int main() {
  SDL_SetMainReady();
  if (SDL_Init(SDL_INIT_VIDEO))
    return 1;
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS,
                      SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);
  auto *w = SDL_CreateWindow("dispatch benchmark", 0, 0, 32, 32,
                             SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
  if (!w)
    return 1;
  auto context = SDL_GL_CreateContext(w);
  if (!context)
    return 1;
  GlApi gl;
  const char *missing = nullptr;
  if (!gl.load(SDL_GL_GetProcAddress, &missing))
    return 1;
  kgfx_gl_config config{SDL_GL_GetProcAddress, nullptr, nullptr};
  kgfx_device_desc desc{KGFX_ABI_VERSION, 32, 32, 3, &config, nullptr, nullptr};
  kgfx_device *d{};
  if (kgfx_device_create(&desc, &d) != KGFX_OK)
    return 1;
  const char *vs = "#version 410 core\nvoid main(){vec2 "
                   "p=vec2((gl_VertexID<<1)&2,gl_VertexID&2);gl_Position=vec4("
                   "p*2.-1.,0.,1.);}";
  const char *fs = "#version 410 core\nout vec4 c;void main(){c=vec4(1);}";
  kgfx_pipeline_desc pd{};
  pd.vertex = {vs, std::strlen(vs)};
  pd.fragment = {fs, std::strlen(fs)};
  pd.samples = 1;
  kgfx_pipeline *p{};
  if (kgfx_pipeline_create(d, &pd, &p) != KGFX_OK) {
    std::puts(kgfx_device_error(d));
    return 1;
  }
  kgfx_frame_begin(d);
  kgfx_pass_desc pass{};
  pass.width = 32;
  pass.height = 32;
  kgfx_pass_begin(d, &pass);
  kgfx_bind_pipeline(d, p);
  constexpr GLenum rasterizer_discard = 0x8C89;
  gl.Enable(rasterizer_discard);
  for (unsigned i = 0; i < 2048; ++i)
    kgfx_draw(d, 0, 3, 1);
  gl.Finish();
  constexpr unsigned count = 20000;
  std::array<double, 9> native{}, rhi{};
  auto measure = [&](bool abstraction) {
    auto start = std::chrono::steady_clock::now();
    if (abstraction) {
      for (unsigned j = 0; j < count; ++j)
        kgfx_draw(d, 0, 3, 1);
    } else {
      for (unsigned j = 0; j < count; ++j)
        gl.DrawArraysInstanced(GL_TRIANGLES, 0, 3, 1);
    }
    auto end = std::chrono::steady_clock::now();
    gl.Finish();
    return std::chrono::duration<double, std::nano>(end - start).count() /
           count;
  };
  for (unsigned i = 0; i < native.size(); ++i) {
    if (i % 2) {
      rhi[i] = measure(true);
      native[i] = measure(false);
    } else {
      native[i] = measure(false);
      rhi[i] = measure(true);
    }
  }
  std::sort(native.begin(), native.end());
  std::sort(rhi.begin(), rhi.end());
  std::printf(
      "{\"renderer\":\"%s\",\"draws_per_sample\":%u,\"native_median_ns\":%.3f,"
      "\"rhi_median_ns\":%.3f,\"difference_ns\":%.3f,\"scope\":\"CPU "
      "submission; rasterization discarded; same GL program/VAO\"}\n",
      kgfx_device_caps(d)->renderer, count, native[4], rhi[4],
      rhi[4] - native[4]);
  gl.Disable(rasterizer_discard);
  kgfx_pass_end(d);
  auto serial = kgfx_submit(d, 0);
  kgfx_wait(d, serial, UINT64_MAX);
  kgfx_pipeline_destroy(d, p);
  kgfx_device_destroy(d);
  SDL_GL_DeleteContext(context);
  SDL_DestroyWindow(w);
  SDL_Quit();
}
