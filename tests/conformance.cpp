// SPDX-License-Identifier: MIT
#include <kstar_gfx/gfx.h>
#include <kstar_gfx/opengl.h>
#define SDL_MAIN_HANDLED
#include <SDL.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <vector>
#if defined(__APPLE__) || defined(__linux__)
#include <dlfcn.h>
#endif

static bool track_allocations = false;
static size_t allocation_count = 0;
void *operator new(size_t n) {
  if (track_allocations) {
#if defined(__APPLE__) || defined(__linux__)
    // Drivers own their allocators. Count only callers in this executable,
    // which statically contains kstar_gfx, not the platform GL driver.
    Dl_info info{};
    if (dladdr(__builtin_return_address(0), &info) && info.dli_fname &&
        std::strstr(info.dli_fname, "kgfx_conformance"))
      ++allocation_count;
#else
    ++allocation_count;
#endif
  }
  if (auto *p = std::malloc(n ? n : 1))
    return p;
  throw std::bad_alloc();
}
void *operator new[](size_t n) { return ::operator new(n); }
void operator delete(void *p) noexcept { std::free(p); }
void operator delete[](void *p) noexcept { std::free(p); }
static kgfx_device *d = nullptr;
#define CHECK(x)                                                               \
  do {                                                                         \
    if (!(x)) {                                                                \
      std::fprintf(stderr, "FAIL %s:%d: %s (%s)\n", __FILE__, __LINE__, #x,    \
                   kgfx_device_error(d));                                      \
      std::exit(1);                                                            \
    }                                                                          \
  } while (0)
#define OK(x) CHECK((x) == KGFX_OK)
static void log_error(void *, const char *m) {
  std::fprintf(stderr, "gfx: %s\n", m);
}
static kgfx_texture *texture(kgfx_format format, unsigned samples = 1) {
  kgfx_texture *t{};
  kgfx_texture_desc desc{
      64, 64, 1, samples, KGFX_TEXTURE_TARGET | KGFX_TEXTURE_SAMPLED, format};
  OK(kgfx_texture_create(d, &desc, &t));
  return t;
}
static kgfx_pipeline *pipeline(const char *fragment,
                               kgfx_blend blend = KGFX_BLEND_OPAQUE,
                               unsigned samples = 1, bool depth = false) {
  const char *vertex = "#version 410 core\nlayout(std140) uniform kgfx_u0 "
                       "{vec4 color;vec4 geometry;};void main(){vec2 "
                       "p=vec2((gl_VertexID<<1)&2,gl_VertexID&2);gl_Position="
                       "vec4(p*2.0-1.0,geometry.x,1.0);}";
  kgfx_pipeline_desc desc{};
  desc.vertex = {vertex, std::strlen(vertex)};
  desc.fragment = {fragment, std::strlen(fragment)};
  desc.color_format = KGFX_RGBA8;
  desc.depth_format = KGFX_DEPTH32F;
  desc.samples = samples;
  desc.blend = blend;
  desc.depth_test = depth;
  desc.depth_write = depth;
  desc.depth_compare = KGFX_COMPARE_LESS;
  kgfx_pipeline *p{};
  OK(kgfx_pipeline_create(d, &desc, &p));
  return p;
}
static void pass(kgfx_texture *t, kgfx_texture *depth = nullptr) {
  kgfx_pass_desc p{};
  p.color = t;
  p.depth = depth;
  p.color_load = KGFX_CLEAR;
  p.depth_load = KGFX_CLEAR;
  p.clear_color[3] = 1;
  p.clear_depth = 1;
  OK(kgfx_pass_begin(d, &p));
}
static kgfx_readback_data capture(kgfx_texture *t, kgfx_readback **r) {
  OK(kgfx_readback_begin(d, t, r));
  auto serial = kgfx_submit(d, 0);
  CHECK(serial);
  OK(kgfx_wait(d, serial, UINT64_MAX));
  kgfx_readback_data data{};
  OK(kgfx_readback_map(d, *r, &data));
  return data;
}
static void pixel(const kgfx_readback_data &data, unsigned x, unsigned y, int r,
                  int g, int b, int a = 255) {
  auto *p = static_cast<const unsigned char *>(data.pixels) +
            y * data.row_pitch + x * 4;
  CHECK(std::abs(int(p[0]) - r) <= 2);
  CHECK(std::abs(int(p[1]) - g) <= 2);
  CHECK(std::abs(int(p[2]) - b) <= 2);
  CHECK(std::abs(int(p[3]) - a) <= 2);
}
int main() {
  SDL_SetMainReady();
  CHECK(SDL_Init(SDL_INIT_VIDEO) == 0);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS,
                      SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);
  auto *w = SDL_CreateWindow("kstar_gfx conformance", 0, 0, 64, 64,
                             SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
  CHECK(w);
  auto context = SDL_GL_CreateContext(w);
  CHECK(context);
  kgfx_gl_config gl{SDL_GL_GetProcAddress, nullptr, nullptr};
  kgfx_device_desc dd{KGFX_ABI_VERSION, 64, 64, 3, &gl, log_error, nullptr};
  OK(kgfx_device_create(&dd, &d));
  std::printf("Renderer: %s\n", kgfx_device_caps(d)->renderer);
  CHECK(kgfx_device_caps(d)->max_samples >= 4);
  auto *color = texture(KGFX_RGBA8);
  auto *copy = texture(KGFX_RGBA8);
  auto *depth = texture(KGFX_DEPTH32F);
  auto *msaa = texture(KGFX_RGBA8, 4);
  // Padded, asymmetric CPU rows must survive upload, LOAD, partial copy and
  // top-first readback. Symmetric clears cannot reveal a flipped upload.
  constexpr size_t upload_pitch = 64 * 4 + 16;
  std::vector<unsigned char> upload(upload_pitch * 64, 0xcc);
  for (unsigned y = 0; y < 64; ++y)
    for (unsigned x = 0; x < 64; ++x) {
      auto *p = upload.data() + y * upload_pitch + x * 4;
      p[0] = y < 32 ? 255 : 0;
      p[1] = x < 32 ? 0 : 255;
      p[2] = y < 32 ? 0 : 255;
      p[3] = 255;
    }
  OK(kgfx_texture_upload(d, color, 0, 0, upload.data(), upload_pitch));
  OK(kgfx_frame_begin(d));
  // Rejected passes must not enter recording or leave a native GL error that
  // poisons the valid LOAD pass immediately afterward.
  kgfx_texture *cube{};
  kgfx_texture_desc cube_desc{16, 16, 1, 1,
                              KGFX_TEXTURE_SAMPLED | KGFX_TEXTURE_CUBE,
                              KGFX_RGBA8};
  OK(kgfx_texture_create(d, &cube_desc, &cube));
  kgfx_pass_desc invalid{};
  invalid.color = cube;
  CHECK(kgfx_pass_begin(d, &invalid) == KGFX_UNSUPPORTED);
  invalid.color = depth;
  CHECK(kgfx_pass_begin(d, &invalid) == KGFX_INVALID_ARGUMENT);
  invalid.color = color;
  invalid.depth = copy;
  CHECK(kgfx_pass_begin(d, &invalid) == KGFX_INVALID_ARGUMENT);
  invalid.color = msaa;
  invalid.depth = depth;
  CHECK(kgfx_pass_begin(d, &invalid) == KGFX_INVALID_ARGUMENT);
  kgfx_texture_destroy(d, cube);
  kgfx_pass_desc loaded{};
  loaded.color = color;
  loaded.color_load = KGFX_LOAD;
  OK(kgfx_pass_begin(d, &loaded));
  kgfx_pass_end(d);
  kgfx_readback *r{};
  auto image = capture(color, &r);
  pixel(image, 16, 16, 255, 0, 0);
  pixel(image, 48, 16, 255, 255, 0);
  pixel(image, 16, 48, 0, 0, 255);
  pixel(image, 48, 48, 0, 255, 255);
  kgfx_readback_destroy(d, r);
  OK(kgfx_frame_begin(d));
  OK(kgfx_texture_copy(d, color, copy, 0, 0, 32, 32, 32, 32));
  image = capture(copy, &r);
  pixel(image, 48, 48, 255, 0, 0);
  kgfx_readback_destroy(d, r);
  const char *frag =
      "#version 410 core\nlayout(std140) uniform kgfx_u0{vec4 color;vec4 "
      "geometry;};out vec4 result;void main(){result=color;}";
  auto *opaque = pipeline(frag);
  auto *alpha = pipeline(frag, KGFX_BLEND_ALPHA);
  auto *zpipe = pipeline(frag, KGFX_BLEND_OPAQUE, 1, true);
  auto *mspipe = pipeline(frag, KGFX_BLEND_OPAQUE, 4);
  size_t stride = kgfx_device_caps(d)->uniform_alignment;
  while (stride < 32)
    stride *= 2;
  std::vector<unsigned char> data(stride * 3);
  float red[8]{1, 0, 0, 1, -.5f, 0, 0, 0}, green[8]{0, 1, 0, .5f, .5f, 0, 0, 0},
      blue[8]{0, 0, 1, 1, .5f, 0, 0, 0};
  std::memcpy(data.data(), red, sizeof(red));
  std::memcpy(data.data() + stride, green, sizeof(green));
  std::memcpy(data.data() + stride * 2, blue, sizeof(blue));
  kgfx_buffer *u{};
  kgfx_buffer_desc bd{data.size(), KGFX_BUFFER_UNIFORM | KGFX_BUFFER_DYNAMIC,
                      data.data()};
  OK(kgfx_buffer_create(d, &bd, &u));
  kgfx_bindings bind{};
  bind.uniforms[0] = u;
  bind.uniform_size[0] = 32;
  OK(kgfx_frame_begin(d));
  OK(kgfx_buffer_update(d, u, 0, data.data(), data.size()));
  pass(color);
  kgfx_bind_pipeline(d, opaque);
  kgfx_bind_resources(d, &bind);
  kgfx_draw(d, 0, 3, 1);
  kgfx_scissor(d, 0, 0, 32, 32);
  kgfx_bind_pipeline(d, alpha);
  bind.uniform_offset[0] = stride;
  kgfx_bind_resources(d, &bind);
  kgfx_draw(d, 0, 3, 1);
  kgfx_pass_end(d);
  // Copy the top-left quadrant to the lower-right; readback rows must be top
  // first.
  OK(kgfx_texture_copy(d, color, copy, 0, 0, 32, 32, 32, 32));
  image = capture(copy, &r);
  pixel(image, 48, 48, 128, 128, 0);
  kgfx_readback_destroy(d, r);
  OK(kgfx_frame_begin(d));
  OK(kgfx_buffer_update(d, u, 0, data.data(), data.size()));
  pass(color, depth);
  bind.uniform_offset[0] = 0;
  kgfx_bind_pipeline(d, zpipe);
  kgfx_bind_resources(d, &bind);
  kgfx_draw(d, 0, 3, 1);
  bind.uniform_offset[0] = stride * 2;
  bind.uniform_size[0] = 0; // Zero means the bytes remaining after this offset.
  kgfx_bind_resources(d, &bind);
  kgfx_draw(d, 0, 3, 1);
  kgfx_pass_end(d);
  image = capture(color, &r);
  pixel(image, 32, 32, 255, 0, 0);
  kgfx_readback_destroy(d, r);
  OK(kgfx_frame_begin(d));
  OK(kgfx_buffer_update(d, u, 0, data.data(), data.size()));
  pass(msaa);
  kgfx_bind_pipeline(d, mspipe);
  bind.uniform_offset[0] = stride * 2;
  kgfx_bind_resources(d, &bind);
  kgfx_draw(d, 0, 3, 1);
  kgfx_pass_end(d);
  OK(kgfx_texture_resolve(d, msaa, color));
  image = capture(color, &r);
  pixel(image, 32, 32, 0, 0, 255);
  kgfx_readback_destroy(d, r);
  bind.uniform_size[0] = 32;
  // 12 frames wrap the three upload slots four times; each capture observes its
  // own data.
  for (unsigned f = 0; f < 12; ++f) {
    OK(kgfx_frame_begin(d));
    red[0] = float(f) / 11;
    red[1] = 1 - red[0];
    std::memcpy(data.data(), red, sizeof(red));
    OK(kgfx_buffer_update(d, u, 0, data.data(), data.size()));
    pass(color);
    kgfx_bind_pipeline(d, opaque);
    bind.uniform_offset[0] = 0;
    kgfx_bind_resources(d, &bind);
    kgfx_draw(d, 0, 3, 1);
    kgfx_pass_end(d);
    image = capture(color, &r);
    pixel(image, 32, 32, std::lround(red[0] * 255), std::lround(red[1] * 255),
          0);
    kgfx_readback_destroy(d, r);
  }
  OK(kgfx_frame_begin(d));
  OK(kgfx_buffer_update(d, u, 0, data.data(), data.size()));
  pass(color);
  kgfx_bind_pipeline(d, opaque);
  kgfx_bind_resources(d, &bind);
  kgfx_draw(d, 0, 3, 1);
  allocation_count = 0;
  track_allocations = true;
  for (int i = 0; i < 1000; ++i) {
    kgfx_bind_pipeline(d, opaque);
    kgfx_bind_resources(d, &bind);
    kgfx_draw(d, 0, 3, 1);
  }
  track_allocations = false;
  CHECK(allocation_count == 0);
  kgfx_pass_end(d);
  // Destroy a used resource before submission: the completion token owns its
  // lifetime.
  kgfx_buffer_destroy(d, u);
  kgfx_pipeline_destroy(d, opaque);
  kgfx_pipeline_destroy(d, alpha);
  kgfx_pipeline_destroy(d, zpipe);
  kgfx_pipeline_destroy(d, mspipe);
  kgfx_texture_destroy(d, color);
  kgfx_texture_destroy(d, copy);
  kgfx_texture_destroy(d, depth);
  kgfx_texture_destroy(d, msaa);
  auto done = kgfx_submit(d, 0);
  OK(kgfx_wait(d, done, UINT64_MAX));
  kgfx_device_wait_idle(d);
  kgfx_stats stats{};
  kgfx_get_stats(d, &stats);
  CHECK(stats.live_buffers == 0 && stats.live_textures == 0 &&
        stats.live_pipelines == 0);
  CHECK(stats.draws >= 1000);
  CHECK(kgfx_is_complete(d, done));
  kgfx_device_destroy(d);
  d = nullptr;
  SDL_GL_DeleteContext(context);
  SDL_DestroyWindow(w);
  SDL_Quit();
  std::puts("PASS: C API, blend/depth, scissor/copy orientation, MSAA resolve, "
            "upload slots, completion retirement, zero library hot-path C++ "
            "allocations");
}
