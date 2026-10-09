#include "gl_api.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <new>
#include <thread>

namespace {
constexpr unsigned max_frames = 8;
enum class Kind { buffer, texture, sampler, pipeline, readback };
struct Resource {
  kgfx_device *owner{};
  Resource *next{}, *previous{}, *retired_next{};
  Kind kind{};
  uint64_t retire_after{};
  bool retired{};
};
struct Format {
  GLenum internal, external, type;
  size_t bytes;
};
Format format(kgfx_format f) {
  switch (f) {
  case KGFX_RGBA8:
    return {GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, 4};
  case KGFX_SRGBA8:
    return {GL_SRGB8_ALPHA8, GL_RGBA, GL_UNSIGNED_BYTE, 4};
  case KGFX_RGBA16F:
    return {GL_RGBA16F, GL_RGBA, GL_HALF_FLOAT, 8};
  case KGFX_RGBA32F:
    return {GL_RGBA32F, GL_RGBA, GL_FLOAT, 16};
  case KGFX_R32F:
    return {GL_R32F, GL_RED, GL_FLOAT, 4};
  case KGFX_DEPTH32F:
    return {GL_DEPTH_COMPONENT32F, GL_DEPTH_COMPONENT, GL_FLOAT, 4};
  }
  return {};
}
GLenum compare(kgfx_compare f) {
  return f == KGFX_COMPARE_LESS     ? GL_LESS
         : f == KGFX_COMPARE_LEQUAL ? GL_LEQUAL
                                    : GL_ALWAYS;
}
} // namespace
struct kgfx_buffer : Resource {
  GLuint names[max_frames]{};
  unsigned count{};
  kgfx_buffer_desc desc{};
};
struct kgfx_texture : Resource {
  GLuint name{};
  GLenum target{};
  kgfx_texture_desc desc{};
  kgfx_access access{};
};
struct kgfx_sampler : Resource {
  GLuint name{};
};
struct kgfx_pipeline : Resource {
  GLuint program{}, vao{};
  kgfx_pipeline_desc desc{};
};
struct kgfx_readback : Resource {
  GLuint pbo{};
  GLsync fence{};
  void *pixels{};
  kgfx_readback_data data{};
  bool ready{};
};
struct kgfx_device {
  GlApi gl{};
  kgfx_gl_config config{};
  kgfx_device_desc desc{};
  kgfx_caps caps{};
  kgfx_stats stats{};
  Resource *resources{}, *retired_resources{};
  char error[2048]{};
  std::thread::id thread{};
  GLuint framebuffer{}, read_framebuffer{}, copy_framebuffer{};
  GLsync fences[max_frames]{};
  uint64_t fence_serial[max_frames]{};
  unsigned frames{3}, slot{};
  uint32_t pass_width{}, pass_height{};
  bool frame{}, pass{};
  kgfx_pipeline *pipeline{};
  kgfx_bindings bindings{};
  GLuint textures[KGFX_MAX_TEXTURE_BINDINGS]{},
      samplers[KGFX_MAX_TEXTURE_BINDINGS]{};
  GLenum texture_targets[KGFX_MAX_TEXTURE_BINDINGS]{};
  void *upload_scratch{};
  size_t upload_capacity{};
};
namespace {
kgfx_result fail(kgfx_device *d, kgfx_result r, const char *message) {
  if (d) {
    std::snprintf(d->error, sizeof(d->error), "%s", message);
    if (d->desc.log)
      d->desc.log(d->desc.log_user, d->error);
  }
  return r;
}
bool thread_ok(kgfx_device *d) {
#ifndef NDEBUG
  if (d && d->thread != std::this_thread::get_id()) {
    fail(d, KGFX_INVALID_ARGUMENT,
         "Graphics operations require the device's owning thread");
    return false;
  }
#endif
  return d != nullptr;
}
bool valid(kgfx_device *d, const Resource *r) {
  return r && r->owner == d && !r->retired;
}
bool hot_valid(kgfx_device *d, const Resource *r) {
#ifndef NDEBUG
  if (!thread_ok(d) || !valid(d, r)) {
    fail(d, KGFX_INVALID_ARGUMENT, "Invalid or retired graphics resource");
    return false;
  }
#else
  (void)d;
  (void)r;
#endif
  return true;
}
void attach(kgfx_device *d, Resource *r, Kind k) {
  r->owner = d;
  r->kind = k;
  r->next = d->resources;
  if (d->resources)
    d->resources->previous = r;
  d->resources = r;
  if (d->frame)
    ++d->stats.steady_state_allocations;
}
GLuint buffer_name(kgfx_device *d, const kgfx_buffer *b) {
  return b->names[b->count == 1 ? 0 : d->slot];
}
void retire(kgfx_device *d, Resource *r) {
  if (!thread_ok(d) || !valid(d, r)) {
    fail(d, KGFX_INVALID_ARGUMENT, "Destroy called with invalid resource");
    return;
  }
  r->retired = true;
  r->retire_after = d->stats.frame_serial + (d->frame ? 1 : 0);
  r->retired_next = d->retired_resources;
  d->retired_resources = r;
  if (r == d->pipeline)
    d->pipeline = nullptr;
  // Binding records are borrowed only while the resource remains owned.
  for (auto &b : d->bindings.vertex)
    if (b == r)
      b = nullptr;
  if (d->bindings.index == r)
    d->bindings.index = nullptr;
  for (auto &b : d->bindings.uniforms)
    if (b == r)
      b = nullptr;
  for (auto &t : d->bindings.textures)
    if (t == r)
      t = nullptr;
  for (auto &s : d->bindings.samplers)
    if (s == r)
      s = nullptr;
}
void release(kgfx_device *d, Resource *r) {
  auto &gl = d->gl;
  switch (r->kind) {
  case Kind::buffer: {
    auto *b = static_cast<kgfx_buffer *>(r);
    gl.DeleteBuffers(b->count, b->names);
    --d->stats.live_buffers;
    delete b;
    break;
  }
  case Kind::texture: {
    auto *t = static_cast<kgfx_texture *>(r);
    gl.DeleteTextures(1, &t->name);
    --d->stats.live_textures;
    delete t;
    break;
  }
  case Kind::sampler: {
    auto *s = static_cast<kgfx_sampler *>(r);
    gl.DeleteSamplers(1, &s->name);
    --d->stats.live_samplers;
    delete s;
    break;
  }
  case Kind::pipeline: {
    auto *p = static_cast<kgfx_pipeline *>(r);
    gl.DeleteProgram(p->program);
    gl.DeleteVertexArrays(1, &p->vao);
    --d->stats.live_pipelines;
    delete p;
    break;
  }
  case Kind::readback: {
    auto *b = static_cast<kgfx_readback *>(r);
    if (b->fence)
      gl.DeleteSync(b->fence);
    gl.DeleteBuffers(1, &b->pbo);
    std::free(b->pixels);
    delete b;
    break;
  }
  }
}
void collect(kgfx_device *d) {
  Resource **link = &d->retired_resources;
  while (*link) {
    auto *r = *link;
    if (r->retire_after <= d->stats.completed_serial) {
      *link = r->retired_next;
      if (r->previous)
        r->previous->next = r->next;
      else
        d->resources = r->next;
      if (r->next)
        r->next->previous = r->previous;
      release(d, r);
    } else
      link = &r->retired_next;
  }
}
void poll(kgfx_device *d) {
  for (unsigned i = 0; i < d->frames; ++i)
    if (d->fences[i]) {
      GLenum result = d->gl.ClientWaitSync(d->fences[i], 0, 0);
      if (result == GL_ALREADY_SIGNALED || result == GL_CONDITION_SATISFIED) {
        d->stats.completed_serial =
            std::max(d->stats.completed_serial, d->fence_serial[i]);
        d->gl.DeleteSync(d->fences[i]);
        d->fences[i] = nullptr;
      }
    }
  collect(d);
}
kgfx_result driver_result(kgfx_device *d, const char *operation) {
  GLenum error = d->gl.GetError();
  if (error) {
    char message[256];
    std::snprintf(message, sizeof(message), "%s: OpenGL error 0x%x", operation,
                  error);
    return fail(d, KGFX_DRIVER_ERROR, message);
  }
  return KGFX_OK;
}
GLuint shader(kgfx_device *d, GLenum stage, const kgfx_shader_artifact &a) {
  if (!a.data || a.size == 0 || a.size > static_cast<size_t>(INT32_MAX)) {
    fail(d, KGFX_INVALID_ARGUMENT, "Shader source is missing or too large");
    return 0;
  }
  auto &gl = d->gl;
  GLuint s = gl.CreateShader(stage);
  const auto *text = static_cast<const char *>(a.data);
  GLint len = static_cast<GLint>(a.size);
  gl.ShaderSource(s, 1, &text, &len);
  gl.CompileShader(s);
  GLint ok{};
  gl.GetShaderiv(s, GL_COMPILE_STATUS, &ok);
  if (!ok) {
    gl.GetShaderInfoLog(s, sizeof(d->error), nullptr, d->error);
    if (d->desc.log)
      d->desc.log(d->desc.log_user, d->error);
    gl.DeleteShader(s);
    return 0;
  }
  return s;
}
void texture_fbo(kgfx_device *d, GLenum bind, GLuint fbo, kgfx_texture *t) {
  auto &gl = d->gl;
  gl.BindFramebuffer(bind, fbo);
  const bool depth = t && t->desc.format == KGFX_DEPTH32F;
  gl.FramebufferTexture2D(bind, GL_COLOR_ATTACHMENT0,
                          (!depth && t) ? t->target : GL_TEXTURE_2D,
                          (!depth && t) ? t->name : 0, 0);
  gl.FramebufferTexture2D(bind, GL_DEPTH_ATTACHMENT,
                          (depth && t) ? t->target : GL_TEXTURE_2D,
                          (depth && t) ? t->name : 0, 0);
  if (bind != GL_READ_FRAMEBUFFER)
    gl.DrawBuffer(depth ? GL_NONE : GL_COLOR_ATTACHMENT0);
  if (bind != GL_DRAW_FRAMEBUFFER)
    gl.ReadBuffer(depth ? GL_NONE : GL_COLOR_ATTACHMENT0);
}
void invalidate_bindings(kgfx_device *d) {
  std::fill(std::begin(d->textures), std::end(d->textures), ~0u);
  std::fill(std::begin(d->samplers), std::end(d->samplers), ~0u);
}
} // namespace

extern "C" const char *kgfx_backend_name(void) { return "OpenGL"; }
extern "C" kgfx_result kgfx_device_create(const kgfx_device_desc *desc,
                                          kgfx_device **out) {
  if (!out)
    return KGFX_INVALID_ARGUMENT;
  *out = nullptr;
  if (!desc || desc->abi_version != KGFX_ABI_VERSION || !desc->backend_config ||
      !desc->width || !desc->height)
    return KGFX_INVALID_ARGUMENT;
  auto *cfg = static_cast<const kgfx_gl_config *>(desc->backend_config);
  if (!cfg->get_proc)
    return KGFX_INVALID_ARGUMENT;
  auto *d = new (std::nothrow) kgfx_device;
  if (!d)
    return KGFX_OUT_OF_MEMORY;
  d->desc = *desc;
  d->config = *cfg;
  d->thread = std::this_thread::get_id();
  d->frames = desc->frames_in_flight ? desc->frames_in_flight : 3;
  if (d->frames > max_frames) {
    delete d;
    return KGFX_INVALID_ARGUMENT;
  }
  const char *missing{};
  if (!d->gl.load(cfg->get_proc, &missing)) {
    fail(d, KGFX_UNSUPPORTED, missing);
    delete d;
    return KGFX_UNSUPPORTED;
  }
  GLint major{}, minor{};
  d->gl.GetIntegerv(GL_MAJOR_VERSION, &major);
  d->gl.GetIntegerv(GL_MINOR_VERSION, &minor);
  if (major < 4 || (major == 4 && minor < 1)) {
    fail(d, KGFX_UNSUPPORTED, "OpenGL 4.1 core is required");
    delete d;
    return KGFX_UNSUPPORTED;
  }
  GLint value{};
  d->gl.GetIntegerv(GL_MAX_TEXTURE_SIZE, &value);
  d->caps.max_texture_size = value;
  d->gl.GetIntegerv(GL_MAX_SAMPLES, &value);
  d->caps.max_samples = value;
  d->gl.GetIntegerv(GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT, &value);
  d->caps.uniform_alignment = value;
  d->gl.GetIntegerv(GL_MAX_TEXTURE_IMAGE_UNITS, &value);
  d->caps.max_texture_bindings =
      std::min<unsigned>(value, KGFX_MAX_TEXTURE_BINDINGS);
  auto *name = d->gl.GetString(GL_RENDERER);
  std::snprintf(d->caps.renderer, sizeof(d->caps.renderer), "%s",
                name ? reinterpret_cast<const char *>(name) : "unknown");
  d->gl.GenFramebuffers(1, &d->framebuffer);
  d->gl.GenFramebuffers(1, &d->read_framebuffer);
  d->gl.GenFramebuffers(1, &d->copy_framebuffer);
  d->gl.Enable(GL_TEXTURE_CUBE_MAP_SEAMLESS);
  invalidate_bindings(d);
  *out = d;
  return KGFX_OK;
}
extern "C" void kgfx_device_wait_idle(kgfx_device *d) {
  if (!thread_ok(d))
    return;
  d->gl.Finish();
  d->stats.completed_serial = d->stats.frame_serial;
  poll(d);
}
extern "C" void kgfx_device_destroy(kgfx_device *d) {
  if (!d)
    return;
  d->gl.Finish();
  while (d->resources) {
    auto *r = d->resources;
    d->resources = r->next;
    release(d, r);
  }
  for (auto f : d->fences)
    if (f)
      d->gl.DeleteSync(f);
  d->gl.DeleteFramebuffers(1, &d->framebuffer);
  d->gl.DeleteFramebuffers(1, &d->read_framebuffer);
  d->gl.DeleteFramebuffers(1, &d->copy_framebuffer);
  std::free(d->upload_scratch);
  delete d;
}
extern "C" const kgfx_caps *kgfx_device_caps(const kgfx_device *d) {
  return d ? &d->caps : nullptr;
}
extern "C" const char *kgfx_device_error(const kgfx_device *d) {
  return d ? d->error : "No graphics device";
}
extern "C" kgfx_result kgfx_device_resize(kgfx_device *d, uint32_t w,
                                          uint32_t h) {
  if (!thread_ok(d) || !w || !h || d->pass)
    return KGFX_INVALID_ARGUMENT;
  d->desc.width = w;
  d->desc.height = h;
  return KGFX_OK;
}
extern "C" kgfx_result kgfx_buffer_create(kgfx_device *d,
                                          const kgfx_buffer_desc *desc,
                                          kgfx_buffer **out) {
  if (!out)
    return KGFX_INVALID_ARGUMENT;
  *out = nullptr;
  if (!thread_ok(d) || !desc || !desc->size ||
      desc->size > static_cast<size_t>(PTRDIFF_MAX))
    return KGFX_INVALID_ARGUMENT;
  auto *b = new (std::nothrow) kgfx_buffer;
  if (!b)
    return KGFX_OUT_OF_MEMORY;
  b->desc = *desc;
  b->desc.initial_data = nullptr;
  b->count = (desc->usage & KGFX_BUFFER_DYNAMIC) ? d->frames : 1;
  d->gl.GenBuffers(b->count, b->names);
  for (unsigned i = 0; i < b->count; ++i) {
    d->gl.BindBuffer(GL_ARRAY_BUFFER, b->names[i]);
    d->gl.BufferData(GL_ARRAY_BUFFER, desc->size, desc->initial_data,
                     b->count > 1 ? GL_DYNAMIC_DRAW : GL_STATIC_DRAW);
  }
  auto result = driver_result(d, "create buffer");
  if (result != KGFX_OK) {
    d->gl.DeleteBuffers(b->count, b->names);
    delete b;
    return result;
  }
  attach(d, b, Kind::buffer);
  ++d->stats.live_buffers;
  *out = b;
  return KGFX_OK;
}
extern "C" kgfx_result kgfx_buffer_update(kgfx_device *d, kgfx_buffer *b,
                                          size_t offset, const void *data,
                                          size_t size) {
  if (!thread_ok(d) || !valid(d, b) || !data || offset > b->desc.size ||
      size > b->desc.size - offset)
    return KGFX_INVALID_ARGUMENT;
  d->gl.BindBuffer(GL_ARRAY_BUFFER, buffer_name(d, b));
  d->gl.BufferSubData(GL_ARRAY_BUFFER, offset, size, data);
  d->stats.upload_bytes += size;
  return KGFX_OK;
}
extern "C" void kgfx_buffer_destroy(kgfx_device *d, kgfx_buffer *b) {
  if (b)
    retire(d, b);
}
extern "C" kgfx_result kgfx_texture_create(kgfx_device *d,
                                           const kgfx_texture_desc *desc,
                                           kgfx_texture **out) {
  if (!out)
    return KGFX_INVALID_ARGUMENT;
  *out = nullptr;
  if (!thread_ok(d) || !desc || !desc->width || !desc->height ||
      !format(desc->format).bytes || desc->width > d->caps.max_texture_size ||
      desc->height > d->caps.max_texture_size)
    return KGFX_INVALID_ARGUMENT;
  const unsigned samples = desc->samples ? desc->samples : 1,
                 mips = desc->mip_levels ? desc->mip_levels : 1;
  if (samples > d->caps.max_samples ||
      (samples > 1 && (mips > 1 || (desc->usage & KGFX_TEXTURE_CUBE))))
    return KGFX_UNSUPPORTED;
  if ((desc->usage & KGFX_TEXTURE_CUBE) && desc->width != desc->height)
    return KGFX_INVALID_ARGUMENT;
  unsigned max_mips = 1;
  for (unsigned n = std::max(desc->width, desc->height); n > 1; n >>= 1)
    ++max_mips;
  if (mips > max_mips)
    return KGFX_INVALID_ARGUMENT;
  auto *t = new (std::nothrow) kgfx_texture;
  if (!t)
    return KGFX_OUT_OF_MEMORY;
  t->desc = *desc;
  t->desc.samples = samples;
  t->desc.mip_levels = mips;
  t->target = samples > 1                         ? GL_TEXTURE_2D_MULTISAMPLE
              : (desc->usage & KGFX_TEXTURE_CUBE) ? GL_TEXTURE_CUBE_MAP
                                                  : GL_TEXTURE_2D;
  auto &gl = d->gl;
  gl.GenTextures(1, &t->name);
  gl.BindTexture(t->target, t->name);
  auto f = format(desc->format);
  if (samples > 1)
    gl.TexImage2DMultisample(t->target, samples, f.internal, desc->width,
                             desc->height, GL_TRUE);
  else {
    const unsigned faces = (desc->usage & KGFX_TEXTURE_CUBE) ? 6 : 1;
    for (unsigned face = 0; face < faces; ++face)
      for (unsigned mip = 0; mip < mips; ++mip)
        gl.TexImage2D(
            faces == 6 ? GL_TEXTURE_CUBE_MAP_POSITIVE_X + face : t->target, mip,
            f.internal, std::max(1u, desc->width >> mip),
            std::max(1u, desc->height >> mip), 0, f.external, f.type, nullptr);
    gl.TexParameteri(t->target, GL_TEXTURE_BASE_LEVEL, 0);
    gl.TexParameteri(t->target, GL_TEXTURE_MAX_LEVEL, mips - 1);
    gl.TexParameteri(t->target, GL_TEXTURE_MIN_FILTER,
                     mips > 1 ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR);
    gl.TexParameteri(t->target, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    gl.TexParameteri(t->target, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    gl.TexParameteri(t->target, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  }
  auto result = driver_result(d, "create texture");
  invalidate_bindings(d);
  if (result != KGFX_OK) {
    gl.DeleteTextures(1, &t->name);
    delete t;
    return result;
  }
  attach(d, t, Kind::texture);
  ++d->stats.live_textures;
  *out = t;
  return KGFX_OK;
}
extern "C" kgfx_result kgfx_texture_upload(kgfx_device *d, kgfx_texture *t,
                                           uint32_t mip, uint32_t face,
                                           const void *pixels, size_t pitch) {
  if (!thread_ok(d) || !valid(d, t) || !pixels || mip >= t->desc.mip_levels ||
      t->desc.samples > 1 ||
      face >= ((t->desc.usage & KGFX_TEXTURE_CUBE) ? 6u : 1u))
    return KGFX_INVALID_ARGUMENT;
  const auto f = format(t->desc.format);
  const unsigned w = std::max(1u, t->desc.width >> mip),
                 h = std::max(1u, t->desc.height >> mip);
  if (!pitch)
    pitch = w * f.bytes;
  if (pitch < w * f.bytes || pitch % f.bytes || pitch / f.bytes > INT32_MAX ||
      pitch > SIZE_MAX / h)
    return KGFX_INVALID_ARGUMENT;
  const size_t input_bytes = pitch * h;
  if (t->target == GL_TEXTURE_2D) {
    // Native GL rows start at the bottom for both uploads and framebuffer
    // writes. Keep that storage convention so copy/readback stay consistent.
    const size_t row_bytes = w * f.bytes, bytes = row_bytes * h;
    if (bytes > d->upload_capacity) {
      void *scratch = std::realloc(d->upload_scratch, bytes);
      if (!scratch)
        return KGFX_OUT_OF_MEMORY;
      d->upload_scratch = scratch;
      d->upload_capacity = bytes;
      if (d->frame)
        ++d->stats.steady_state_allocations;
    }
    auto *target = static_cast<unsigned char *>(d->upload_scratch);
    auto *source = static_cast<const unsigned char *>(pixels);
    for (unsigned y = 0; y < h; ++y)
      std::memcpy(target + y * row_bytes, source + (h - 1 - y) * pitch,
                  row_bytes);
    pixels = target;
    pitch = row_bytes;
  }
  auto &gl = d->gl;
  gl.BindTexture(t->target, t->name);
  gl.PixelStorei(GL_UNPACK_ALIGNMENT, 1);
  gl.PixelStorei(GL_UNPACK_ROW_LENGTH, pitch / f.bytes);
  gl.TexSubImage2D(t->target == GL_TEXTURE_CUBE_MAP
                       ? GL_TEXTURE_CUBE_MAP_POSITIVE_X + face
                       : t->target,
                   mip, 0, 0, w, h, f.external, f.type, pixels);
  gl.PixelStorei(GL_UNPACK_ROW_LENGTH, 0);
  d->stats.upload_bytes += input_bytes;
  invalidate_bindings(d);
  return driver_result(d, "texture upload");
}
extern "C" void kgfx_texture_destroy(kgfx_device *d, kgfx_texture *t) {
  if (t)
    retire(d, t);
}
extern "C" kgfx_result kgfx_sampler_create(kgfx_device *d,
                                           const kgfx_sampler_desc *desc,
                                           kgfx_sampler **out) {
  if (!out)
    return KGFX_INVALID_ARGUMENT;
  *out = nullptr;
  if (!thread_ok(d) || !desc)
    return KGFX_INVALID_ARGUMENT;
  auto *s = new (std::nothrow) kgfx_sampler;
  if (!s)
    return KGFX_OUT_OF_MEMORY;
  auto &gl = d->gl;
  gl.GenSamplers(1, &s->name);
  gl.SamplerParameteri(
      s->name, GL_TEXTURE_MIN_FILTER,
      desc->mipmapped
          ? (desc->min_filter == KGFX_LINEAR ? GL_LINEAR_MIPMAP_LINEAR
                                             : GL_NEAREST_MIPMAP_NEAREST)
          : (desc->min_filter == KGFX_LINEAR ? GL_LINEAR : GL_NEAREST));
  gl.SamplerParameteri(s->name, GL_TEXTURE_MAG_FILTER,
                       desc->mag_filter == KGFX_LINEAR ? GL_LINEAR
                                                       : GL_NEAREST);
  gl.SamplerParameteri(s->name, GL_TEXTURE_WRAP_S,
                       desc->wrap_u == KGFX_REPEAT ? GL_REPEAT
                                                   : GL_CLAMP_TO_EDGE);
  gl.SamplerParameteri(s->name, GL_TEXTURE_WRAP_T,
                       desc->wrap_v == KGFX_REPEAT ? GL_REPEAT
                                                   : GL_CLAMP_TO_EDGE);
  gl.SamplerParameteri(s->name, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
  gl.SamplerParameteri(s->name, GL_TEXTURE_COMPARE_MODE,
                       desc->compare_enabled ? GL_COMPARE_REF_TO_TEXTURE
                                             : GL_NONE);
  gl.SamplerParameteri(s->name, GL_TEXTURE_COMPARE_FUNC,
                       compare(desc->compare));
  attach(d, s, Kind::sampler);
  std::fill(std::begin(d->samplers), std::end(d->samplers), ~0u);
  ++d->stats.live_samplers;
  *out = s;
  return KGFX_OK;
}
extern "C" void kgfx_sampler_destroy(kgfx_device *d, kgfx_sampler *s) {
  if (s)
    retire(d, s);
}
extern "C" kgfx_result kgfx_pipeline_create(kgfx_device *d,
                                            const kgfx_pipeline_desc *desc,
                                            kgfx_pipeline **out) {
  if (!out)
    return KGFX_INVALID_ARGUMENT;
  *out = nullptr;
  if (!thread_ok(d) || !desc ||
      desc->attribute_count > KGFX_MAX_VERTEX_ATTRIBUTES)
    return KGFX_INVALID_ARGUMENT;
  for (unsigned i = 0; i < desc->attribute_count; ++i)
    if (desc->attributes[i].buffer_slot > 1 ||
        desc->attributes[i].location >= 16)
      return KGFX_INVALID_ARGUMENT;
  auto &pgl = d->gl;
  GLuint vs = shader(d, GL_VERTEX_SHADER, desc->vertex);
  if (!vs)
    return KGFX_DRIVER_ERROR;
  GLuint fs =
      desc->fragment.data ? shader(d, GL_FRAGMENT_SHADER, desc->fragment) : 0;
  if (desc->fragment.data && !fs) {
    pgl.DeleteShader(vs);
    return KGFX_DRIVER_ERROR;
  }
  auto *p = new (std::nothrow) kgfx_pipeline;
  if (!p) {
    pgl.DeleteShader(vs);
    if (fs)
      pgl.DeleteShader(fs);
    return KGFX_OUT_OF_MEMORY;
  }
  p->desc = *desc;
  p->desc.vertex = {};
  p->desc.fragment = {};
  p->program = pgl.CreateProgram();
  pgl.AttachShader(p->program, vs);
  if (fs)
    pgl.AttachShader(p->program, fs);
  pgl.LinkProgram(p->program);
  pgl.DeleteShader(vs);
  if (fs)
    pgl.DeleteShader(fs);
  GLint ok{};
  pgl.GetProgramiv(p->program, GL_LINK_STATUS, &ok);
  if (!ok) {
    pgl.GetProgramInfoLog(p->program, sizeof(d->error), nullptr, d->error);
    if (d->desc.log)
      d->desc.log(d->desc.log_user, d->error);
    pgl.DeleteProgram(p->program);
    delete p;
    return KGFX_DRIVER_ERROR;
  }
  pgl.UseProgram(p->program);
  for (unsigned i = 0; i < KGFX_MAX_UNIFORM_BINDINGS; ++i) {
    char name[32];
    std::snprintf(name, sizeof(name), "kgfx_u%u", i);
    GLuint index = pgl.GetUniformBlockIndex(p->program, name);
    if (index != GL_INVALID_INDEX)
      pgl.UniformBlockBinding(p->program, index, i);
  }
  for (unsigned i = 0; i < KGFX_MAX_TEXTURE_BINDINGS; ++i) {
    char name[32];
    std::snprintf(name, sizeof(name), "kgfx_t%u", i);
    GLint loc = pgl.GetUniformLocation(p->program, name);
    if (loc >= 0)
      pgl.Uniform1i(loc, i);
  }
  pgl.GenVertexArrays(1, &p->vao);
  pgl.BindVertexArray(p->vao);
  for (unsigned i = 0; i < desc->attribute_count; ++i)
    pgl.EnableVertexAttribArray(desc->attributes[i].location);
  attach(d, p, Kind::pipeline);
  ++d->stats.live_pipelines;
  d->pipeline = nullptr;
  *out = p;
  return KGFX_OK;
}
extern "C" void kgfx_pipeline_destroy(kgfx_device *d, kgfx_pipeline *p) {
  if (p)
    retire(d, p);
}
extern "C" kgfx_result kgfx_frame_begin(kgfx_device *d) {
  if (!thread_ok(d) || d->frame)
    return KGFX_INVALID_ARGUMENT;
  poll(d);
  d->slot = d->stats.frame_serial % d->frames;
  if (d->fences[d->slot]) {
    auto result = kgfx_wait(d, d->fence_serial[d->slot], UINT64_MAX);
    if (result != KGFX_OK)
      return result;
  }
  d->frame = true;
  d->pipeline = nullptr;
  invalidate_bindings(d);
  return KGFX_OK;
}
extern "C" kgfx_result kgfx_pass_begin(kgfx_device *d,
                                       const kgfx_pass_desc *desc) {
  if (!thread_ok(d) || !desc || !d->frame || d->pass)
    return KGFX_INVALID_ARGUMENT;
  if ((desc->color && !valid(d, desc->color)) ||
      (desc->depth && !valid(d, desc->depth)))
    return KGFX_INVALID_ARGUMENT;
  // The pass API has no cubemap face selector.
  if ((desc->color && desc->color->target == GL_TEXTURE_CUBE_MAP) ||
      (desc->depth && desc->depth->target == GL_TEXTURE_CUBE_MAP))
    return KGFX_UNSUPPORTED;
  if (desc->color && desc->color->desc.format == KGFX_DEPTH32F)
    return KGFX_INVALID_ARGUMENT;
  if (desc->depth && desc->depth->desc.format != KGFX_DEPTH32F)
    return KGFX_INVALID_ARGUMENT;
  if (desc->color && desc->depth &&
      (desc->color->desc.width != desc->depth->desc.width ||
       desc->color->desc.height != desc->depth->desc.height ||
       desc->color->desc.samples != desc->depth->desc.samples))
    return KGFX_INVALID_ARGUMENT;
  auto &gl = d->gl;
  if (desc->color || desc->depth) {
    gl.BindFramebuffer(GL_FRAMEBUFFER, d->framebuffer);
    gl.FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                            desc->color ? desc->color->target : GL_TEXTURE_2D,
                            desc->color ? desc->color->name : 0, 0);
    gl.FramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                            desc->depth ? desc->depth->target : GL_TEXTURE_2D,
                            desc->depth ? desc->depth->name : 0, 0);
    gl.DrawBuffer(desc->color ? GL_COLOR_ATTACHMENT0 : GL_NONE);
    gl.ReadBuffer(desc->color ? GL_COLOR_ATTACHMENT0 : GL_NONE);
    if (gl.CheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
      return fail(d, KGFX_DRIVER_ERROR, "Incomplete render pass framebuffer");
  } else {
    gl.BindFramebuffer(GL_FRAMEBUFFER, 0);
    gl.DrawBuffer(GL_BACK);
    gl.ReadBuffer(GL_BACK);
  }
  auto *t = desc->color ? desc->color : desc->depth;
  d->pass_width = desc->width ? desc->width : t ? t->desc.width : d->desc.width;
  d->pass_height = desc->height ? desc->height
                   : t          ? t->desc.height
                                : d->desc.height;
  gl.Viewport(0, 0, d->pass_width, d->pass_height);
  gl.Disable(GL_SCISSOR_TEST);
  if (desc->color && desc->color->desc.format == KGFX_SRGBA8)
    gl.Enable(GL_FRAMEBUFFER_SRGB);
  else
    gl.Disable(GL_FRAMEBUFFER_SRGB);
  GLbitfield mask = 0;
  if (desc->color_load == KGFX_CLEAR && (desc->color || !desc->depth)) {
    gl.ColorMask(1, 1, 1, 1);
    gl.ClearColor(desc->clear_color[0], desc->clear_color[1],
                  desc->clear_color[2], desc->clear_color[3]);
    mask |= GL_COLOR_BUFFER_BIT;
  }
  if (desc->depth_load == KGFX_CLEAR) {
    gl.DepthMask(1);
    gl.ClearDepth(desc->clear_depth);
    mask |= GL_DEPTH_BUFFER_BIT;
  }
  if (mask)
    gl.Clear(mask);
  d->pass = true;
  d->pipeline = nullptr;
  return KGFX_OK;
}
extern "C" void kgfx_pass_end(kgfx_device *d) {
  if (d)
    d->pass = false;
}
extern "C" void kgfx_bind_pipeline(kgfx_device *d, kgfx_pipeline *p) {
  if (!hot_valid(d, p))
    return;
  if (d->pipeline == p)
    return;
  auto &gl = d->gl;
  const auto &s = p->desc;
  gl.UseProgram(p->program);
  gl.BindVertexArray(p->vao);
  if (s.depth_test)
    gl.Enable(GL_DEPTH_TEST);
  else
    gl.Disable(GL_DEPTH_TEST);
  gl.DepthMask(s.depth_write ? 1 : 0);
  gl.DepthFunc(compare(s.depth_compare));
  if (s.cull == KGFX_CULL_NONE)
    gl.Disable(GL_CULL_FACE);
  else {
    gl.Enable(GL_CULL_FACE);
    gl.CullFace(s.cull == KGFX_CULL_BACK ? GL_BACK : GL_FRONT);
    gl.FrontFace(GL_CCW);
  }
  if (s.blend == KGFX_BLEND_OPAQUE)
    gl.Disable(GL_BLEND);
  else {
    gl.Enable(GL_BLEND);
    gl.BlendEquation(GL_FUNC_ADD);
    gl.BlendFuncSeparate(
        s.blend == KGFX_BLEND_PREMULTIPLIED ? GL_ONE : GL_SRC_ALPHA,
        s.blend == KGFX_BLEND_ADDITIVE ? GL_ONE : GL_ONE_MINUS_SRC_ALPHA,
        GL_ONE,
        s.blend == KGFX_BLEND_ADDITIVE ? GL_ONE : GL_ONE_MINUS_SRC_ALPHA);
  }
  gl.PolygonMode(GL_FRONT_AND_BACK, s.wireframe ? GL_LINE : GL_FILL);
  d->pipeline = p;
  ++d->stats.pipeline_binds;
}
extern "C" void kgfx_bind_resources(kgfx_device *d, const kgfx_bindings *b) {
  if (!d || !b || !d->pipeline)
    return;
  auto &gl = d->gl;
  auto &p = d->pipeline->desc;
  for (unsigned i = 0; i < p.attribute_count; ++i) {
    const auto &a = p.attributes[i];
    auto *buffer = b->vertex[a.buffer_slot];
    if (!buffer || !hot_valid(d, buffer))
      return;
    gl.BindBuffer(GL_ARRAY_BUFFER, buffer_name(d, buffer));
    int n = a.type == KGFX_FLOAT2 ? 2 : a.type == KGFX_FLOAT3 ? 3 : 4;
    gl.VertexAttribPointer(
        a.location, n, a.type == KGFX_UNORM8X4 ? GL_UNSIGNED_BYTE : GL_FLOAT,
        a.type == KGFX_UNORM8X4 ? 1 : 0, p.vertex_stride[a.buffer_slot],
        reinterpret_cast<const void *>(b->vertex_offset[a.buffer_slot] +
                                       a.offset));
    gl.VertexAttribDivisor(a.location, a.divisor);
  }
  if (b->index) {
    if (!hot_valid(d, b->index))
      return;
    gl.BindBuffer(GL_ELEMENT_ARRAY_BUFFER, buffer_name(d, b->index));
  }
  for (unsigned i = 0; i < KGFX_MAX_UNIFORM_BINDINGS; ++i)
    if (auto *u = b->uniforms[i]) {
      if (!hot_valid(d, u))
        return;
      const size_t offset = b->uniform_offset[i];
#ifndef NDEBUG
      if (offset >= u->desc.size) {
        fail(d, KGFX_INVALID_ARGUMENT, "Uniform offset is outside its buffer");
        return;
      }
#endif
      size_t size =
          b->uniform_size[i] ? b->uniform_size[i] : u->desc.size - offset;
#ifndef NDEBUG
      if (b->uniform_offset[i] % d->caps.uniform_alignment ||
          b->uniform_offset[i] > u->desc.size ||
          size > u->desc.size - b->uniform_offset[i]) {
        fail(d, KGFX_INVALID_ARGUMENT,
             "Uniform range is unaligned or outside its buffer");
        return;
      }
#endif
      gl.BindBufferRange(GL_UNIFORM_BUFFER, i, buffer_name(d, u),
                         b->uniform_offset[i], size);
    }
  for (unsigned i = 0; i < KGFX_MAX_TEXTURE_BINDINGS; ++i) {
    auto *t = b->textures[i];
    auto *s = b->samplers[i];
    if (t && !hot_valid(d, t))
      return;
    if (s && !hot_valid(d, s))
      return;
    GLuint name = t ? t->name : 0;
    if (d->textures[i] != name) {
      gl.ActiveTexture(GL_TEXTURE0 + i);
      if (d->texture_targets[i] && (!t || d->texture_targets[i] != t->target))
        gl.BindTexture(d->texture_targets[i], 0);
      if (t)
        gl.BindTexture(t->target, name);
      d->textures[i] = name;
      d->texture_targets[i] = t ? t->target : 0;
    }
    GLuint sn = s ? s->name : 0;
    if (d->samplers[i] != sn) {
      gl.BindSampler(i, sn);
      d->samplers[i] = sn;
    }
  }
  d->bindings = *b;
}
extern "C" void kgfx_viewport(kgfx_device *d, int32_t x, int32_t y, uint32_t w,
                              uint32_t h) {
  if (d)
    d->gl.Viewport(
        x, static_cast<int>(d->pass_height) - y - static_cast<int>(h), w, h);
}
extern "C" void kgfx_scissor(kgfx_device *d, int32_t x, int32_t y, uint32_t w,
                             uint32_t h) {
  if (d) {
    d->gl.Enable(GL_SCISSOR_TEST);
    d->gl.Scissor(x, static_cast<int>(d->pass_height) - y - static_cast<int>(h),
                  w, h);
  }
}
extern "C" void kgfx_draw(kgfx_device *d, uint32_t first, uint32_t count,
                          uint32_t instances) {
  if (!d || !d->pipeline || !d->pass || !count || !instances)
    return;
  d->gl.DrawArraysInstanced(GL_TRIANGLES, first, count, instances);
  ++d->stats.draws;
  d->stats.instances += instances;
}
extern "C" void kgfx_draw_indexed(kgfx_device *d, uint32_t first,
                                  uint32_t count, uint32_t instances) {
  if (!d || !d->pipeline || !d->pass || !d->bindings.index || !count ||
      !instances)
    return;
  const unsigned size = d->bindings.index_size;
  if (size != 2 && size != 4) {
    fail(d, KGFX_INVALID_ARGUMENT, "Index size must be 2 or 4");
    return;
  }
  d->gl.DrawElementsInstanced(
      GL_TRIANGLES, count, size == 2 ? GL_UNSIGNED_SHORT : GL_UNSIGNED_INT,
      reinterpret_cast<const void *>(d->bindings.index_offset +
                                     static_cast<size_t>(first) * size),
      instances);
  ++d->stats.draws;
  d->stats.instances += instances;
}
extern "C" kgfx_result kgfx_texture_transition(kgfx_device *d, kgfx_texture *t,
                                               kgfx_access access) {
  if (!thread_ok(d) || !valid(d, t) || d->pass)
    return KGFX_INVALID_ARGUMENT;
  // GL 4.1 orders framebuffer writes and later sampling across distinct passes.
  // Simultaneous read/write feedback is forbidden by this API.
  t->access = access;
  return KGFX_OK;
}
extern "C" kgfx_result kgfx_texture_copy(kgfx_device *d, kgfx_texture *src,
                                         kgfx_texture *dst, uint32_t sx,
                                         uint32_t sy, uint32_t dx, uint32_t dy,
                                         uint32_t w, uint32_t h) {
  if (!thread_ok(d) || !valid(d, src) || !valid(d, dst) || d->pass ||
      src == dst || src->desc.format != dst->desc.format ||
      src->target == GL_TEXTURE_CUBE_MAP ||
      dst->target == GL_TEXTURE_CUBE_MAP ||
      src->desc.samples != dst->desc.samples || sx > src->desc.width ||
      sy > src->desc.height || dx > dst->desc.width || dy > dst->desc.height ||
      w > src->desc.width - sx || h > src->desc.height - sy ||
      w > dst->desc.width - dx || h > dst->desc.height - dy)
    return KGFX_INVALID_ARGUMENT;
  texture_fbo(d, GL_READ_FRAMEBUFFER, d->read_framebuffer, src);
  texture_fbo(d, GL_DRAW_FRAMEBUFFER, d->copy_framebuffer, dst);
  d->gl.Disable(GL_SCISSOR_TEST);
  d->gl.BlitFramebuffer(sx, src->desc.height - sy - h, sx + w,
                        src->desc.height - sy, dx, dst->desc.height - dy - h,
                        dx + w, dst->desc.height - dy,
                        src->desc.format == KGFX_DEPTH32F ? GL_DEPTH_BUFFER_BIT
                                                          : GL_COLOR_BUFFER_BIT,
                        GL_NEAREST);
  return driver_result(d, "texture copy");
}
extern "C" kgfx_result kgfx_texture_resolve(kgfx_device *d, kgfx_texture *src,
                                            kgfx_texture *dst) {
  if (!thread_ok(d) || !valid(d, src) || !valid(d, dst) || d->pass ||
      src->desc.samples < 2 || dst->desc.samples != 1 ||
      src->desc.width != dst->desc.width ||
      src->desc.height != dst->desc.height ||
      src->desc.format != dst->desc.format)
    return KGFX_INVALID_ARGUMENT;
  texture_fbo(d, GL_READ_FRAMEBUFFER, d->read_framebuffer, src);
  texture_fbo(d, GL_DRAW_FRAMEBUFFER, d->copy_framebuffer, dst);
  d->gl.Disable(GL_SCISSOR_TEST);
  d->gl.BlitFramebuffer(0, 0, src->desc.width, src->desc.height, 0, 0,
                        dst->desc.width, dst->desc.height,
                        src->desc.format == KGFX_DEPTH32F ? GL_DEPTH_BUFFER_BIT
                                                          : GL_COLOR_BUFFER_BIT,
                        GL_NEAREST);
  return driver_result(d, "texture resolve");
}
extern "C" kgfx_completion kgfx_submit(kgfx_device *d, uint32_t present) {
  if (!thread_ok(d) || !d->frame || d->pass)
    return 0;
  auto begin = std::chrono::steady_clock::now();
  const uint64_t serial = ++d->stats.frame_serial;
  d->fences[d->slot] = d->gl.FenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
  d->fence_serial[d->slot] = serial;
  d->gl.Flush();
  if (present && d->config.present)
    d->config.present(d->config.user);
  d->frame = false;
  d->stats.cpu_submit_ms = std::chrono::duration<double, std::milli>(
                               std::chrono::steady_clock::now() - begin)
                               .count();
  poll(d);
  return serial;
}
extern "C" uint32_t kgfx_is_complete(kgfx_device *d, kgfx_completion serial) {
  if (!thread_ok(d))
    return 0;
  poll(d);
  return serial <= d->stats.completed_serial;
}
extern "C" kgfx_result kgfx_wait(kgfx_device *d, kgfx_completion serial,
                                 uint64_t timeout) {
  if (!thread_ok(d) || serial > d->stats.frame_serial)
    return KGFX_INVALID_ARGUMENT;
  if (serial <= d->stats.completed_serial)
    return KGFX_OK;
  unsigned best = max_frames;
  for (unsigned i = 0; i < d->frames; ++i)
    if (d->fences[i] && d->fence_serial[i] >= serial &&
        (best == max_frames || d->fence_serial[i] < d->fence_serial[best]))
      best = i;
  if (best == max_frames)
    return fail(d, KGFX_DRIVER_ERROR, "Completion fence is missing");
  auto result = d->gl.ClientWaitSync(d->fences[best],
                                     GL_SYNC_FLUSH_COMMANDS_BIT, timeout);
  if (result == GL_WAIT_FAILED)
    return fail(d, KGFX_DRIVER_ERROR, "GPU completion wait failed");
  if (result != GL_ALREADY_SIGNALED && result != GL_CONDITION_SATISFIED)
    return KGFX_NOT_READY;
  poll(d);
  return KGFX_OK;
}
extern "C" kgfx_result kgfx_readback_begin(kgfx_device *d, kgfx_texture *t,
                                           kgfx_readback **out) {
  if (!out)
    return KGFX_INVALID_ARGUMENT;
  *out = nullptr;
  if (!thread_ok(d) || !valid(d, t) || d->pass || t->desc.samples != 1 ||
      t->target != GL_TEXTURE_2D)
    return KGFX_INVALID_ARGUMENT;
  const auto f = format(t->desc.format);
  auto *b = new (std::nothrow) kgfx_readback;
  if (!b)
    return KGFX_OUT_OF_MEMORY;
  b->data = {nullptr,
             static_cast<size_t>(t->desc.width) * t->desc.height * f.bytes,
             static_cast<size_t>(t->desc.width) * f.bytes,
             t->desc.width,
             t->desc.height,
             t->desc.format};
  b->pixels = std::malloc(b->data.size);
  if (!b->pixels) {
    delete b;
    return KGFX_OUT_OF_MEMORY;
  }
  auto &gl = d->gl;
  gl.GenBuffers(1, &b->pbo);
  gl.BindBuffer(GL_PIXEL_PACK_BUFFER, b->pbo);
  gl.BufferData(GL_PIXEL_PACK_BUFFER, b->data.size, nullptr, GL_STREAM_READ);
  texture_fbo(d, GL_READ_FRAMEBUFFER, d->read_framebuffer, t);
  gl.PixelStorei(GL_PACK_ALIGNMENT, 1);
  gl.PixelStorei(GL_PACK_ROW_LENGTH, 0);
  gl.ReadPixels(0, 0, t->desc.width, t->desc.height, f.external, f.type,
                nullptr);
  gl.BindBuffer(GL_PIXEL_PACK_BUFFER, 0);
  b->fence = gl.FenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
  gl.Flush();
  if (driver_result(d, "readback") != KGFX_OK) {
    gl.DeleteSync(b->fence);
    gl.DeleteBuffers(1, &b->pbo);
    std::free(b->pixels);
    delete b;
    return KGFX_DRIVER_ERROR;
  }
  attach(d, b, Kind::readback);
  d->stats.readback_bytes += b->data.size;
  *out = b;
  return KGFX_OK;
}
extern "C" kgfx_result kgfx_readback_map(kgfx_device *d, kgfx_readback *b,
                                         kgfx_readback_data *out) {
  if (!thread_ok(d) || !valid(d, b) || !out)
    return KGFX_INVALID_ARGUMENT;
  if (!b->ready) {
    auto &gl = d->gl;
    auto status = gl.ClientWaitSync(b->fence, 0, 0);
    if (status == GL_WAIT_FAILED)
      return KGFX_DRIVER_ERROR;
    if (status != GL_ALREADY_SIGNALED && status != GL_CONDITION_SATISFIED)
      return KGFX_NOT_READY;
    gl.BindBuffer(GL_PIXEL_PACK_BUFFER, b->pbo);
    auto *source = static_cast<const unsigned char *>(gl.MapBufferRange(
        GL_PIXEL_PACK_BUFFER, 0, b->data.size, GL_MAP_READ_BIT));
    if (!source) {
      gl.BindBuffer(GL_PIXEL_PACK_BUFFER, 0);
      return KGFX_DRIVER_ERROR;
    }
    auto *dest = static_cast<unsigned char *>(b->pixels);
    for (unsigned y = 0; y < b->data.height; ++y)
      std::memcpy(dest + y * b->data.row_pitch,
                  source + (b->data.height - 1 - y) * b->data.row_pitch,
                  b->data.row_pitch);
    gl.UnmapBuffer(GL_PIXEL_PACK_BUFFER);
    gl.BindBuffer(GL_PIXEL_PACK_BUFFER, 0);
    b->data.pixels = b->pixels;
    b->ready = true;
  }
  *out = b->data;
  return KGFX_OK;
}
extern "C" void kgfx_readback_destroy(kgfx_device *d, kgfx_readback *b) {
  if (b)
    retire(d, b);
}
extern "C" void kgfx_get_stats(const kgfx_device *d, kgfx_stats *out) {
  if (d && out)
    *out = d->stats;
}
