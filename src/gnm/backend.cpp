// kstar_gfx GNM backend. Direct definitions: no runtime backend selection.
#include "shaders/resolve.inc"
#include <gnm_drawcommandbuffer.h>
#include <gnm_helpers.h>
#include <gnmdriver.h>
#include <gpuaddr.h>
#include <kstar_gfx/gfx.h>
#include <kstar_gfx/gnm.h>
#include <math.h>
#include <orbis/Sysmodule.h>
#include <orbis/VideoOut.h>
#include <orbis/libkernel.h>
#include <pm4/amdgfxregs.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

namespace {
constexpr uint32_t max_frames = 4;
enum resource_kind {
  buffer_kind,
  texture_kind,
  sampler_kind,
  pipeline_kind,
  readback_kind
};
struct resource {
  kgfx_device *owner{};
  resource *next{};
  uint64_t retire{};
  resource_kind kind{};
  bool dead{};
};
struct frame {
  GnmDirectMemory command{}, upload{}, label{};
  GnmCommandBuffer cmd{};
  size_t used{};
  uint64_t serial{}, presented{};
};
struct shader {
  void *container{};
  GnmShaderMetadata meta{};
  GnmDirectMemory code{};
};
static uint64_t now_ns() { return sceKernelGetProcessTime() * 1000ull; }
static size_t align_up(size_t n, size_t a) { return (n + a - 1) & ~(a - 1); }
static uint32_t pixel_bytes(kgfx_format f) {
  return f == KGFX_RGBA16F ? 8 : f == KGFX_RGBA32F ? 16 : 4;
}
static GnmDataFormat format(kgfx_format f) {
  switch (f) {
  case KGFX_SRGBA8:
    return GNM_FMT_R8G8B8A8_SRGB;
  case KGFX_RGBA16F:
    return GNM_FMT_R16G16B16A16_FLOAT;
  case KGFX_RGBA32F:
    return GNM_FMT_R32G32B32A32_FLOAT;
  case KGFX_R32F:
  case KGFX_DEPTH32F:
    return GNM_FMT_R32_FLOAT;
  default:
    return GNM_FMT_R8G8B8A8_UNORM;
  }
}
static GnmDepthCompare compare(kgfx_compare c) {
  return c == KGFX_COMPARE_LESS     ? GNM_DEPTH_COMPARE_LESS
         : c == KGFX_COMPARE_LEQUAL ? GNM_DEPTH_COMPARE_LESSEQUAL
                                    : GNM_DEPTH_COMPARE_ALWAYS;
}
static bool allocate(GnmDirectMemory &m, size_t n, size_t a = 65536) {
  return sceGnmDirectMemoryAllocate(&m, align_up(n, 65536),
                                    a < 65536 ? 65536 : a,
                                    GNM_DIRECT_MEMORY_TYPE_WC_GARLIC,
                                    GNM_PROT_CPU_GPU_RW) == GNM_ERROR_OK;
}
} // namespace
struct kgfx_buffer : resource {
  GnmDirectMemory memory{};
  size_t size{};
  uint32_t usage{};
  void *current{};
  void *shadow{};
  uint64_t version_serial{};
  bool transient{};
};
struct kgfx_texture : resource {
  GnmDirectMemory memory{}, htile{};
  kgfx_texture_desc desc{};
  GnmTexture tex{};
  GnmRenderTarget color{};
  GnmDepthRenderTarget depth{};
  size_t bytes{}, pitch{};
  bool display{}, depth_compressed{};
  float depth_clear{1};
  size_t htile_bytes{};
};
struct kgfx_sampler : resource {
  GnmSampler native{};
};
struct kgfx_pipeline : resource {
  kgfx_pipeline_desc desc{};
  shader vs{}, ps{};
  GnmVsStageRegisters vsregs{};
  GnmPsStageRegisters psregs{};
  GnmDirectMemory fetch{};
  GnmBlendControl blend{};
  GnmDepthStencilControl depth{};
  GnmPrimitiveSetup raster{};
};
struct kgfx_readback : resource {
  GnmDirectMemory memory{};
  void *linear{};
  kgfx_readback_data data{};
  GnmTexture tex{};
  size_t bytes{}, source_pitch{};
  bool display{};
  uint64_t ready{};
};
struct kgfx_device {
  kgfx_caps caps{};
  kgfx_stats stats{};
  kgfx_log_fn log{};
  void *log_user{};
  char error[256]{};
  frame frames[max_frames]{};
  uint32_t frame_count{}, slot{}, width{}, height{};
  bool active{}, inpass{}, failed{}, present{}, reported_present{};
  uint64_t serial{}, completed{};
  uint64_t last_presented{}, present_count{};
  resource *resources{};
  GnmVideoOut video{};
  kgfx_texture display[max_frames]{};
  kgfx_pipeline *pipeline{}, *resolve_pipeline{}, *resolve_pipeline32{},
      *depth_pipeline{};
  kgfx_pipeline *clear_pipeline{}, *clear_pipeline32{}, *copy_pipeline{};
  kgfx_buffer *clear_uniform{};
  kgfx_bindings bindings{};
};
namespace {
static kgfx_result fail(kgfx_device *d, kgfx_result r, const char *s) {
  if (d) {
    snprintf(d->error, sizeof(d->error), "%s", s);
    if (d->log)
      d->log(d->log_user, s);
  }
  return r;
}
static kgfx_result wait_flip(kgfx_device *d, uint64_t serial) {
  if (!serial)
    return KGFX_OK;
  const uint64_t began = now_ns();
  for (;;) {
    OrbisVideoOutFlipStatus status{};
    if (sceVideoOutGetFlipStatus(d->video.handle, &status) < 0)
      return fail(d, KGFX_DRIVER_ERROR, "Cannot query VideoOut flip completion");
    if (status.flipArg >= 0 && uint64_t(status.flipArg) >= serial) {
      if (!d->reported_present && status.num && d->log) {
        d->log(d->log_user, "GNM_PRESENT_PROGRESS completed=1");
        d->reported_present = true;
      }
      return KGFX_OK;
    }
    if (now_ns() - began > 30'000'000'000ull)
      return fail(d, KGFX_DRIVER_ERROR, "Timed out waiting for VideoOut flip completion");
    sceKernelUsleep(100);
  }
}
static GnmCommandBuffer *cmd(kgfx_device *d) { return &d->frames[d->slot].cmd; }
static bool room(kgfx_device *d, size_t words = 256) {
  if (!d || !d->active || d->failed)
    return false;
  auto *c = cmd(d);
  if (size_t(c->endptr - c->cmdptr) < words + 32) {
    d->failed = true;
    fail(d, KGFX_OUT_OF_MEMORY,
         "GNM command arena exhausted; increase command_bytes");
    return false;
  }
  return true;
}
static void *staging(kgfx_device *d, size_t n, size_t alignment = 256) {
  auto &f = d->frames[d->slot];
  auto at = align_up(f.used, alignment);
  if (at + n > f.upload.size) {
    fail(d, KGFX_OUT_OF_MEMORY,
         "GNM upload arena exhausted; increase upload_bytes");
    return nullptr;
  }
  f.used = at + n;
  return static_cast<unsigned char *>(f.upload.mapped) + at;
}
static bool prepare_buffer(kgfx_device *d, kgfx_buffer *b) {
  if (!b || !b->transient || b->version_serial == d->serial + 1)
    return true;
  void *copy = staging(d, b->size);
  if (!copy) {
    d->failed = true;
    return false;
  }
  memcpy(copy, b->shadow, b->size);
  b->current = copy;
  b->version_serial = d->serial + 1;
  return true;
}
static void register_resource(kgfx_device *d, resource *r, resource_kind k) {
  r->owner = d;
  if (d->active)
    ++d->stats.steady_state_allocations;
  r->kind = k;
  r->next = d->resources;
  d->resources = r;
}
static void release_shader(shader &s) {
  sceGnmDirectMemoryRelease(&s.code);
  free(s.container);
  s = {};
}
static void free_resource(resource *r) {
  switch (r->kind) {
  case buffer_kind: {
    auto *p = static_cast<kgfx_buffer *>(r);
    sceGnmDirectMemoryRelease(&p->memory);
    if (p->owner)
      --p->owner->stats.live_buffers;
    free(p->shadow);
    delete p;
    break;
  }
  case texture_kind: {
    auto *p = static_cast<kgfx_texture *>(r);
    sceGnmDirectMemoryRelease(&p->memory);
    sceGnmDirectMemoryRelease(&p->htile);
    if (p->owner)
      --p->owner->stats.live_textures;
    delete p;
    break;
  }
  case sampler_kind:
    if (r->owner)
      --r->owner->stats.live_samplers;
    delete static_cast<kgfx_sampler *>(r);
    break;
  case pipeline_kind: {
    auto *p = static_cast<kgfx_pipeline *>(r);
    release_shader(p->vs);
    release_shader(p->ps);
    sceGnmDirectMemoryRelease(&p->fetch);
    if (p->owner)
      --p->owner->stats.live_pipelines;
    delete p;
    break;
  }
  case readback_kind: {
    auto *p = static_cast<kgfx_readback *>(r);
    sceGnmDirectMemoryRelease(&p->memory);
    free(p->linear);
    delete p;
    break;
  }
  }
}
static void collect(kgfx_device *d) {
  resource **p = &d->resources;
  while (*p) {
    auto *r = *p;
    if (r->dead && r->retire <= d->completed) {
      *p = r->next;
      free_resource(r);
    } else
      p = &r->next;
  }
}
static void retire(kgfx_device *d, resource *r) {
  if (!d || !r || r->owner != d || r->dead)
    return;
  r->dead = true;
  r->retire = d->serial + (d->active ? 1 : 0);
}
static void barrier(kgfx_device *d) {
  sceGnmDrawCmdWaitGraphicsWrite(
      cmd(d),
      GnmAcquireTargetFlags(GNM_ACQUIRE_TARGET_CB0 | GNM_ACQUIRE_TARGET_DB));
}
// Packages retain the upstream FP16 SB prefix and optionally append a native
// FP32 export variant. Selection happens once while creating a pipeline.
static kgfx_shader_artifact fragment_variant(const kgfx_shader_artifact &a,
                                             kgfx_format fmt) {
  if ((fmt != KGFX_RGBA32F && fmt != KGFX_R32F) || !a.data || a.size < 16)
    return a;
  uint32_t footer[4];
  memcpy(footer, static_cast<const unsigned char *>(a.data) + a.size - 16, 16);
  if (footer[0] != 0x5846474b || footer[1] != 1)
    return a;
  if (footer[2] > a.size - 16 || footer[3] != a.size - 16 - footer[2])
    return {};
  return {static_cast<const unsigned char *>(a.data) + footer[2], footer[3]};
}
static bool load_shader(shader &s, const kgfx_shader_artifact &a,
                        GnmShaderType type) {
  if (!a.data || !a.size)
    return false;
  s.container = malloc(a.size);
  if (!s.container)
    return false;
  memcpy(s.container, a.data, a.size);
  if (sceGnmShaderBinaryGetMetadata(s.container, a.size, &s.meta) !=
          GNM_ERROR_OK ||
      s.meta.type != type || !s.meta.shadercodesize)
    return false;
  size_t code_size = sceGnmShaderCommonCodeSize(s.meta.common);
  size_t offset = static_cast<const unsigned char *>(s.meta.shadercode) -
                  static_cast<const unsigned char *>(s.container);
  if (offset > a.size || code_size > a.size - offset)
    return false;
  if (!allocate(s.code, code_size))
    return false;
  memcpy(s.code.mapped, s.meta.shadercode, code_size);
  return true;
}
static void clear_color(kgfx_device *d, kgfx_texture *t, const float value[4]) {
  // A render clear preserves the attachment's native tiling and cache
  // ownership. Constants are versioned in the current frame arena, just like
  // user uploads.
  barrier(d);
  sceGnmDrawCmdSetRenderTarget(cmd(d), 0, &t->color);
  sceGnmDrawCmdSetRenderTargetMask(cmd(d), 15);
  sceGnmDrawCmdSetDepthRenderTarget(cmd(d), nullptr);
  kgfx_viewport(d, 0, 0, t->desc.width, t->desc.height);
  kgfx_scissor(d, 0, 0, t->desc.width, t->desc.height);
  kgfx_bind_pipeline(d, t->desc.format == KGFX_RGBA32F ||
                                t->desc.format == KGFX_R32F
                            ? d->clear_pipeline32
                            : d->clear_pipeline);
  if (kgfx_buffer_update(d, d->clear_uniform, 0, value, 16) != KGFX_OK) {
    d->failed = true;
    return;
  }
  kgfx_bindings binding{};
  binding.uniforms[0] = d->clear_uniform;
  binding.uniform_size[0] = 16;
  kgfx_bind_resources(d, &binding);
  d->inpass = true;
  kgfx_draw(d, 0, 3, 1);
  d->inpass = false;
  barrier(d);
}
static void context_register(kgfx_device *d, uint32_t reg, uint32_t value) {
  auto *c = cmd(d);
  *c->cmdptr++ = 0xc0016900; // SET_CONTEXT_REG, one register.
  *c->cmdptr++ = (reg - 0x28000) / 4;
  *c->cmdptr++ = value;
}
static void multisample_state(kgfx_device *d, uint32_t samples) {
  const uint32_t n = samples == 4 ? 2 : 0;
  context_register(d, R_028BE0_PA_SC_AA_CONFIG,
                   S_028BE0_MSAA_NUM_SAMPLES(n) |
                       S_028BE0_MSAA_EXPOSED_SAMPLES(n) |
                       S_028BE0_MAX_SAMPLE_DIST(n ? 6 : 0));
  context_register(d, R_028804_DB_EQAA,
                   S_028804_MAX_ANCHOR_SAMPLES(n) |
                       S_028804_MASK_EXPORT_NUM_SAMPLES(n) |
                       S_028804_ALPHA_TO_MASK_NUM_SAMPLES(n) |
                       S_028804_HIGH_QUALITY_INTERSECTIONS(1) |
                       S_028804_STATIC_ANCHOR_ASSOCIATIONS(1));
  // Standard rotated grid, in sixteenths of a pixel. Identical in each quad
  // pixel.
  for (uint32_t pixel = 0; pixel < 4; ++pixel)
    for (uint32_t word = 0; word < 4; ++word)
      context_register(d, 0x028bf8 + pixel * 16 + word * 4,
                       samples == 4 && word == 0 ? 0x622ae6ae : 0);
  context_register(d, 0x028c38, 0xffffffff);
  context_register(d, 0x028c3c, 0xffffffff);
}
// Base-GFX7 depth expansion: disable depth/stencil compression during a
// rectangle draw. This is the pre-GFX8 path; DECOMPRESS_ENABLE is not used.
static bool expand_depth(kgfx_device *d, kgfx_texture *t) {
  if (!t || t->desc.format != KGFX_DEPTH32F || !t->depth_compressed)
    return true;
  if (d->inpass || !room(d, 256))
    return false;
  barrier(d);
  multisample_state(d, t->desc.samples);
  sceGnmDrawCmdSetRenderTarget(cmd(d), 0, nullptr);
  sceGnmDrawCmdSetRenderTargetMask(cmd(d), 0);
  sceGnmDrawCmdSetDepthRenderTarget(cmd(d), &t->depth);
  sceGnmDrawCmdSetDepthClearValue(cmd(d), t->depth_clear);
  kgfx_viewport(d, 0, 0, t->desc.width, t->desc.height);
  kgfx_scissor(d, 0, 0, t->desc.width, t->desc.height);
  kgfx_bind_pipeline(d, d->depth_pipeline);
  kgfx_bindings bindings{};
  kgfx_bind_resources(d, &bindings);
  GnmDepthStencilControl depth{};
  sceGnmDrawCmdSetDepthStencilControl(cmd(d), &depth);
  GnmDbRenderControl control{};
  control.depthwritebackpol = control.stencilwritebackpol = true;
  sceGnmDrawCmdSetDbRenderControl(cmd(d), &control);
  context_register(d, R_02800C_DB_RENDER_OVERRIDE,
                   S_02800C_FORCE_HIS_ENABLE0(V_02800C_FORCE_DISABLE) |
                       S_02800C_FORCE_HIS_ENABLE1(V_02800C_FORCE_DISABLE));
  sceGnmDrawCmdSetPrimitiveType(cmd(d), GNM_PT_RECTLIST);
  d->inpass = true;
  kgfx_draw(d, 0, 3, 1);
  d->inpass = false;
  barrier(d);
  sceGnmDrawCmdSetPrimitiveType(cmd(d), GNM_PT_TRILIST);
  control = {};
  sceGnmDrawCmdSetDbRenderControl(cmd(d), &control);
  context_register(d, R_02800C_DB_RENDER_OVERRIDE, 0);
  sceGnmDrawCmdSetDepthRenderTarget(cmd(d), nullptr);
  t->depth_compressed = false;
  return !d->failed;
}
static bool owned(kgfx_device *d, resource *r) {
  return r && r->owner == d && !r->dead;
}
} // namespace
extern "C" {
const char *kgfx_backend_name(void) { return "GNM"; }
kgfx_result kgfx_device_create(const kgfx_device_desc *desc,
                               kgfx_device **out) {
  if (!desc || !out || desc->abi_version != KGFX_ABI_VERSION || !desc->width ||
      !desc->height)
    return KGFX_INVALID_ARGUMENT;
  *out = nullptr;
  auto *d = new kgfx_device{};
  d->log = desc->log;
  d->log_user = desc->log_user;
  d->width = desc->width;
  d->height = desc->height;
  d->frame_count = desc->frames_in_flight ? desc->frames_in_flight : 3;
  if (d->frame_count > max_frames) {
    delete d;
    return KGFX_INVALID_ARGUMENT;
  }
  kgfx_gnm_config defaults{1, 1024 * 1024, 32 * 1024 * 1024};
  const auto *c =
      desc->backend_config
          ? static_cast<const kgfx_gnm_config *>(desc->backend_config)
          : &defaults;
  d->present = c->present != 0;
  sceSysmoduleLoadModuleInternal(ORBIS_SYSMODULE_INTERNAL_VIDEO_OUT);
  if (d->present) {
    GnmVideoOutCreateInfo vi{};
    sceGnmVideoOutInitDefaultCreateInfo(&vi, d->width, d->height);
    vi.numbuffers = d->frame_count;
    if (sceGnmVideoOutOpen(&d->video, &vi) != GNM_ERROR_OK) {
      delete d;
      return KGFX_DRIVER_ERROR;
    }
    for (uint32_t i = 0; i < d->frame_count; ++i) {
      auto &t = d->display[i];
      t.owner = d;
      t.display = true;
      // The API's backbuffer accepts display-encoded RGBA bytes, like the GL
      // default framebuffer. VideoOut interprets them as sRGB at scanout;
      // using an sRGB render view here would encode the final image twice.
      t.desc = {d->width, d->height, 1, 1, KGFX_TEXTURE_TARGET, KGFX_RGBA8};
      t.memory.mapped = d->video.buffers[i];
      t.bytes = d->video.buffersize;
      t.pitch = d->video.pitch * 4;
      sceGnmRtCreateColorTarget(&t.color, t.memory.mapped,
                                GNM_FMT_R8G8B8A8_UNORM, d->width, d->height, 1,
                                1, 1, GNM_TM_DISPLAY_LINEAR_ALIGNED,
                                GNM_GPU_BASE, nullptr, nullptr);
    }
  }
  for (uint32_t i = 0; i < d->frame_count; ++i) {
    auto &f = d->frames[i];
    if (!allocate(f.command, c->command_bytes ? c->command_bytes
                                              : defaults.command_bytes) ||
        !allocate(f.upload,
                  c->upload_bytes ? c->upload_bytes : defaults.upload_bytes) ||
        !allocate(f.label, 65536)) {
      kgfx_device_destroy(d);
      return KGFX_OUT_OF_MEMORY;
    }
    memset(f.label.mapped, 0, 8);
    f.cmd = sceGnmCmdInit(f.command.mapped, uint32_t(f.command.size), nullptr,
                          nullptr);
  }
  kgfx_pipeline_desc resolve_desc{};
  resolve_desc.vertex = {resolve_vert, sizeof(resolve_vert)};
  resolve_desc.fragment = {resolve_frag, sizeof(resolve_frag)};
  resolve_desc.color_format = KGFX_RGBA16F;
  resolve_desc.samples = 1;
  if (kgfx_pipeline_create(d, &resolve_desc, &d->resolve_pipeline) != KGFX_OK) {
    kgfx_device_destroy(d);
    return KGFX_DRIVER_ERROR;
  }
  resolve_desc.color_format = KGFX_RGBA32F;
  if (kgfx_pipeline_create(d, &resolve_desc, &d->resolve_pipeline32) !=
      KGFX_OK) {
    kgfx_device_destroy(d);
    return KGFX_DRIVER_ERROR;
  }
  kgfx_pipeline_desc clear_desc{};
  clear_desc.vertex = {resolve_vert, sizeof(resolve_vert)};
  clear_desc.fragment = {clear_color_frag, sizeof(clear_color_frag)};
  clear_desc.color_format = KGFX_RGBA16F;
  clear_desc.samples = 1;
  if (kgfx_pipeline_create(d, &clear_desc, &d->clear_pipeline) != KGFX_OK) {
    kgfx_device_destroy(d);
    return KGFX_DRIVER_ERROR;
  }
  clear_desc.color_format = KGFX_RGBA32F;
  if (kgfx_pipeline_create(d, &clear_desc, &d->clear_pipeline32) != KGFX_OK) {
    kgfx_device_destroy(d);
    return KGFX_DRIVER_ERROR;
  }
  clear_desc.fragment = {copy_color_frag, sizeof(copy_color_frag)};
  if (kgfx_pipeline_create(d, &clear_desc, &d->copy_pipeline) != KGFX_OK) {
    kgfx_device_destroy(d);
    return KGFX_DRIVER_ERROR;
  }
  kgfx_buffer_desc clear_buffer{16, KGFX_BUFFER_UNIFORM | KGFX_BUFFER_DYNAMIC,
                                nullptr};
  if (kgfx_buffer_create(d, &clear_buffer, &d->clear_uniform) != KGFX_OK) {
    kgfx_device_destroy(d);
    return KGFX_OUT_OF_MEMORY;
  }
  kgfx_pipeline_desc depth_desc{};
  depth_desc.vertex = {resolve_vert, sizeof(resolve_vert)};
  depth_desc.fragment = {clear_depth_frag, sizeof(clear_depth_frag)};
  depth_desc.color_format = KGFX_RGBA8;
  depth_desc.depth_format = KGFX_DEPTH32F;
  depth_desc.samples = 1;
  depth_desc.depth_test = depth_desc.depth_write = 1;
  depth_desc.depth_compare = KGFX_COMPARE_ALWAYS;
  if (kgfx_pipeline_create(d, &depth_desc, &d->depth_pipeline) != KGFX_OK) {
    kgfx_device_destroy(d);
    return KGFX_DRIVER_ERROR;
  }
  d->caps = {16384, 4, 16, KGFX_MAX_TEXTURE_BINDINGS,
             "AMD Liverpool / OpenGNM"};
  *out = d;
  return KGFX_OK;
}
void kgfx_device_destroy(kgfx_device *d) {
  if (!d)
    return;
  kgfx_device_wait_idle(d);
  while (d->resources) {
    auto *r = d->resources;
    d->resources = r->next;
    free_resource(r);
  }
  for (auto &f : d->frames) {
    sceGnmDirectMemoryRelease(&f.command);
    sceGnmDirectMemoryRelease(&f.upload);
    sceGnmDirectMemoryRelease(&f.label);
  }
  if (d->present) {
    OrbisVideoOutFlipStatus status{};
    if (sceVideoOutGetFlipStatus(d->video.handle, &status) >= 0 && d->log) {
      char message[128];
      snprintf(message, sizeof(message), "GNM_PRESENT submitted=%llu completed=%llu",
               static_cast<unsigned long long>(d->present_count),
               static_cast<unsigned long long>(status.num));
      d->log(d->log_user, message);
    }
    sceGnmVideoOutClose(&d->video);
  }
  delete d;
}
const kgfx_caps *kgfx_device_caps(const kgfx_device *d) {
  return d ? &d->caps : nullptr;
}
const char *kgfx_device_error(const kgfx_device *d) {
  return d ? d->error : "null device";
}
kgfx_result kgfx_device_resize(kgfx_device *d, uint32_t w, uint32_t h) {
  if (!d || !w || !h)
    return KGFX_INVALID_ARGUMENT;
  if (w == d->width && h == d->height)
    return KGFX_OK;
  if (!d->present && !d->active) {
    d->width = w;
    d->height = h;
    return KGFX_OK;
  }
  return fail(d, KGFX_UNSUPPORTED,
              "VideoOut dimensions are fixed for device lifetime");
}
void kgfx_device_wait_idle(kgfx_device *d) {
  if (!d)
    return;
  if (d->active)
    kgfx_submit(d, 0);
  kgfx_wait(d, d->serial, UINT64_MAX);
  if (d->present)
    wait_flip(d, d->last_presented);
  collect(d);
}
kgfx_result kgfx_buffer_create(kgfx_device *d, const kgfx_buffer_desc *x,
                               kgfx_buffer **out) {
  if (!d || !x || !out || !x->size || x->size > UINT32_MAX)
    return KGFX_INVALID_ARGUMENT;
  auto *b = new kgfx_buffer{};
  if (!allocate(b->memory, x->size)) {
    delete b;
    return KGFX_OUT_OF_MEMORY;
  }
  b->shadow = malloc(x->size);
  if (!b->shadow) {
    sceGnmDirectMemoryRelease(&b->memory);
    delete b;
    return KGFX_OUT_OF_MEMORY;
  }
  b->size = x->size;
  b->usage = x->usage;
  b->current = b->memory.mapped;
  memset(b->current, 0, x->size);
  if (x->initial_data)
    memcpy(b->current, x->initial_data, x->size);
  memcpy(b->shadow, b->current, b->size);
  register_resource(d, b, buffer_kind);
  ++d->stats.live_buffers;
  *out = b;
  return KGFX_OK;
}
kgfx_result kgfx_buffer_update(kgfx_device *d, kgfx_buffer *b, size_t offset,
                               const void *p, size_t n) {
  if (!owned(d, b) || !p || offset > b->size || n > b->size - offset)
    return KGFX_INVALID_ARGUMENT;
  if (d->active) {
    void *dst = staging(d, b->size);
    if (!dst)
      return KGFX_OUT_OF_MEMORY;
    memcpy(dst, b->shadow, b->size);
    memcpy(static_cast<unsigned char *>(dst) + offset, p, n);
    b->current = dst;
    b->transient = true;
    b->version_serial = d->serial + 1;
  } else {
    kgfx_device_wait_idle(d);
    memcpy(static_cast<unsigned char *>(b->memory.mapped) + offset, p, n);
    b->current = b->memory.mapped;
  }
  memcpy(static_cast<unsigned char *>(b->shadow) + offset, p, n);
  d->stats.upload_bytes += n;
  return KGFX_OK;
}
void kgfx_buffer_destroy(kgfx_device *d, kgfx_buffer *b) {
  if (owned(d, b)) {
    retire(d, b);
  }
}
kgfx_result kgfx_texture_create(kgfx_device *d, const kgfx_texture_desc *x,
                                kgfx_texture **out) {
  if (!d || !x || !out || !x->width || !x->height || x->width > 16384 ||
      x->height > 16384 || !x->mip_levels)
    return KGFX_INVALID_ARGUMENT;
  if ((x->samples > 1 && x->samples != 4) ||
      (x->samples > 1 &&
       (x->mip_levels != 1 || (x->usage & KGFX_TEXTURE_CUBE))))
    return fail(d, KGFX_INVALID_ARGUMENT,
                "GNM supports 1x and 4x MSAA, with one mip");
  if ((x->format == KGFX_DEPTH32F &&
       (x->mip_levels != 1 || (x->usage & KGFX_TEXTURE_CUBE))) ||
      ((x->usage & KGFX_TEXTURE_CUBE) && (x->usage & KGFX_TEXTURE_TARGET)))
    return fail(d, KGFX_UNSUPPORTED,
                "GNM attachments are 2D with one depth mip");
  uint32_t max_mips = 1;
  for (uint32_t extent = x->width > x->height ? x->width : x->height;
       extent > 1; extent >>= 1)
    ++max_mips;
  if (x->mip_levels > max_mips ||
      ((x->usage & KGFX_TEXTURE_CUBE) && x->width != x->height))
    return KGFX_INVALID_ARGUMENT;
  auto *t = new kgfx_texture{};
  t->desc = *x;
  if (!t->desc.samples)
    t->desc.samples = 1;
  uint64_t bytes = 0;
  uint32_t alignment = 0;
  GnmTileMode tile = x->format == KGFX_DEPTH32F
                         ? GNM_TM_DEPTH_2D_THIN_256
                         : (x->samples > 1 ? GNM_TM_THIN_2D_THIN
                                           : GNM_TM_DISPLAY_LINEAR_ALIGNED);
  GnmTextureCreateInfo ci{};
  sceGnmTexInit2dCreateInfo(&ci, format(x->format), x->width, x->height,
                            x->mip_levels, tile, GNM_GPU_BASE);
  if (x->usage & KGFX_TEXTURE_CUBE) {
    ci.texturetype = GNM_TEXTURE_CUBEMAP;
    ci.numslices = 1; // OpenGNM counts cubes here, then expands to six faces.
  }
  if (x->samples > 1) {
    ci.texturetype = GNM_TEXTURE_2D_MSAA;
    ci.numfragments = x->samples;
  }
  if (sceGnmCreateTexture(&t->tex, &ci) != GNM_ERROR_OK ||
      sceGnmTexCalcByteSize(&bytes, &alignment, &t->tex) != GNM_ERROR_OK) {
    delete t;
    return fail(d, KGFX_UNSUPPORTED, "Unsupported GNM texture layout");
  }
  const uint64_t texture_bytes = bytes;
  const uint32_t texture_alignment = alignment;
  if (x->format == KGFX_DEPTH32F) {
    GnmDepthRenderTargetCreateInfo di{};
    di.width = x->width;
    di.height = x->height;
    di.numslices = 1;
    di.zfmt = GNM_Z_32_FLOAT;
    di.tilemodehint = tile;
    di.mingpumode = GNM_GPU_BASE;
    di.numfragments = t->desc.samples;
    di.flags.enable_htile_acceleration = 1;
    if (sceGnmCreateDepthRenderTarget(&t->depth, &di) != GNM_ERROR_OK ||
        sceGnmDrtCalcByteSize(&bytes, &alignment, &t->depth) != GNM_ERROR_OK) {
      delete t;
      return KGFX_UNSUPPORTED;
    }
  } else if (!(x->usage & KGFX_TEXTURE_CUBE)) {
    if (sceGnmRtCreateColorTarget(&t->color, nullptr, format(x->format),
                                  x->width, x->height, 1, t->desc.samples,
                                  t->desc.samples, tile, GNM_GPU_BASE, &bytes,
                                  &alignment) != GNM_ERROR_OK) {
      delete t;
      return KGFX_UNSUPPORTED;
    }
  }
  // The attachment describes level zero; sampled texture storage also includes
  // every mip. Never truncate the latter to the base render-target footprint.
  if (bytes < texture_bytes)
    bytes = texture_bytes;
  if (alignment < texture_alignment)
    alignment = texture_alignment;
  if (!allocate(t->memory, bytes, alignment)) {
    delete t;
    return KGFX_OUT_OF_MEMORY;
  }
  t->bytes = bytes;
  t->pitch = size_t(sceGnmTexGetPitch(&t->tex)) * pixel_bytes(x->format);
  memset(t->memory.mapped, 0, bytes);
  sceGnmTexSetBaseAddress(&t->tex, t->memory.mapped);
  if (x->format == KGFX_DEPTH32F) {
    sceGnmDrtSetZReadAddress(&t->depth, t->memory.mapped);
    sceGnmDrtSetZWriteAddress(&t->depth, t->memory.mapped);
    GpaHtileParams hp{};
    hp.pitch = sceGnmDrtGetPaddedWidth(&t->depth);
    hp.height = sceGnmDrtGetPaddedHeight(&t->depth);
    hp.numslices = 1;
    hp.numfrags = t->desc.samples;
    hp.bpp = 32;
    hp.arraymode = t->depth.depthinfo.arraymode;
    hp.banks = t->depth.depthinfo.numbanks;
    hp.pipeconfig = t->depth.depthinfo.pipeconfig;
    hp.mingpumode = GNM_GPU_BASE;
    GpaHtileInfo hi{};
    if (sceGpaComputeHtileInfo(&hi, &hp) != GPA_ERR_OK ||
        !allocate(t->htile, hi.htilebytes, hi.basealign)) {
      sceGnmDirectMemoryRelease(&t->memory);
      delete t;
      return KGFX_OUT_OF_MEMORY;
    }
    t->htile_bytes = hi.htilebytes;
    auto *metadata = static_cast<uint32_t *>(t->htile.mapped);
    for (size_t i = 0; i < hi.htilebytes / 4; ++i)
      metadata[i] =
          0xfffffff0; // Depth-only: fast-cleared to the initial 1.0 value.
    t->depth_compressed = true;
    sceGnmDrtSetHtileAddress(&t->depth, t->htile.mapped);
  } else if (!(x->usage & KGFX_TEXTURE_CUBE))
    sceGnmRtSetBaseAddr(&t->color, t->memory.mapped);
  register_resource(d, t, texture_kind);
  ++d->stats.live_textures;
  *out = t;
  return KGFX_OK;
}
kgfx_result kgfx_texture_upload(kgfx_device *d, kgfx_texture *t, uint32_t mip,
                                uint32_t face, const void *p, size_t pitch) {
  if (!owned(d, t) || !p || mip >= t->desc.mip_levels || t->desc.samples > 1 ||
      face >= ((t->desc.usage & KGFX_TEXTURE_CUBE) ? 6u : 1u))
    return KGFX_INVALID_ARGUMENT;
  if (t->desc.format == KGFX_DEPTH32F)
    return fail(d, KGFX_UNSUPPORTED,
                "GNM depth uploads require a depth rendering pass");
  auto info = sceGnmTexBuildInfo(&t->tex);
  uint64_t bytes = 0, offset = 0;
  if (sceGpaComputeSurfaceSizeOffset(&bytes, &offset, &info, mip, face) !=
      GPA_ERR_OK)
    return KGFX_UNSUPPORTED;
  GpaTilingParams tp{};
  GpaSurfaceInfo surface{};
  sceGpaTpInit(&tp, &info, mip, face);
  sceGpaComputeSurfaceInfo(&surface, &tp);
  const uint32_t width = (t->desc.width >> mip) ? t->desc.width >> mip : 1,
                 height = (t->desc.height >> mip) ? t->desc.height >> mip : 1,
                 bpp = pixel_bytes(t->desc.format);
  if (!pitch)
    pitch = size_t(width) * bpp;
  if (pitch < size_t(width) * bpp)
    return KGFX_INVALID_ARGUMENT;
  void *dest = d->active
                   ? staging(d, bytes)
                   : static_cast<unsigned char *>(t->memory.mapped) + offset;
  if (!dest)
    return KGFX_OUT_OF_MEMORY;
  if (!d->active)
    kgfx_device_wait_idle(d);
  GpaSurfaceContext context{};
  if (sceGpaInitSurfaceContext(&context, bytes, &tp) != GPA_ERR_OK)
    return KGFX_UNSUPPORTED;
  for (uint32_t y = 0; y < height; ++y)
    for (uint32_t x = 0; x < width; ++x) {
      uint64_t at, bit;
      if (sceGpaComputeSurfaceCoord(&at, &bit, &context, x, y, 0, 0) !=
              GPA_ERR_OK ||
          at + bpp > bytes)
        return KGFX_DRIVER_ERROR;
      memcpy(static_cast<unsigned char *>(dest) + at,
             static_cast<const unsigned char *>(p) + y * pitch + x * bpp, bpp);
    }
  if (d->active) {
    if (!room(d, 7 * (bytes / 0x1ffffc + 1)))
      return KGFX_OUT_OF_MEMORY;
    barrier(d);
    sceGnmDrawCmdCopyMemory(
        cmd(d), reinterpret_cast<uint64_t>(t->memory.mapped) + offset,
        reinterpret_cast<uint64_t>(dest), uint32_t(bytes));
    barrier(d);
  }
  d->stats.upload_bytes += size_t(width) * height * bpp;
  return KGFX_OK;
}
void kgfx_texture_destroy(kgfx_device *d, kgfx_texture *t) {
  if (owned(d, t)) {
    retire(d, t);
  }
}
kgfx_result kgfx_sampler_create(kgfx_device *d, const kgfx_sampler_desc *x,
                                kgfx_sampler **out) {
  if (!d || !x || !out)
    return KGFX_INVALID_ARGUMENT;
  auto *s = new kgfx_sampler{};
  s->native.clampx = x->wrap_u == KGFX_REPEAT ? GNM_TEX_CLAMP_WRAP
                                              : GNM_TEX_CLAMP_CLAMP_LAST_TEXEL;
  s->native.clampy = x->wrap_v == KGFX_REPEAT ? GNM_TEX_CLAMP_WRAP
                                              : GNM_TEX_CLAMP_CLAMP_LAST_TEXEL;
  s->native.clampz = GNM_TEX_CLAMP_CLAMP_LAST_TEXEL;
  s->native.xyminfilter =
      x->min_filter == KGFX_LINEAR ? GNM_FILTER_BILINEAR : GNM_FILTER_POINT;
  s->native.xymagfilter =
      x->mag_filter == KGFX_LINEAR ? GNM_FILTER_BILINEAR : GNM_FILTER_POINT;
  s->native.mipfilter =
      x->mipmapped ? GNM_MIPFILTER_LINEAR : GNM_MIPFILTER_NONE;
  s->native.maxlod = x->mipmapped ? 4095 : 0;
  s->native.depthcomparefunc =
      compare(x->compare_enabled ? x->compare : KGFX_COMPARE_ALWAYS);
  register_resource(d, s, sampler_kind);
  ++d->stats.live_samplers;
  *out = s;
  return KGFX_OK;
}
void kgfx_sampler_destroy(kgfx_device *d, kgfx_sampler *s) {
  if (owned(d, s)) {
    retire(d, s);
  }
}
kgfx_result kgfx_pipeline_create(kgfx_device *d, const kgfx_pipeline_desc *x,
                                 kgfx_pipeline **out) {
  if (!d || !x || !out || x->attribute_count > KGFX_MAX_VERTEX_ATTRIBUTES ||
      (x->samples > 1 && x->samples != 4))
    return KGFX_INVALID_ARGUMENT;
  uint32_t used_locations = 0;
  for (uint32_t i = 0; i < x->attribute_count; ++i) {
    const auto &attribute = x->attributes[i];
    if (attribute.location >= KGFX_MAX_VERTEX_ATTRIBUTES ||
        attribute.buffer_slot >= 2 ||
        (used_locations & (1u << attribute.location)) ||
        !x->vertex_stride[attribute.buffer_slot])
      return KGFX_INVALID_ARGUMENT;
    if (attribute.divisor > 1)
      return fail(d, KGFX_UNSUPPORTED,
                  "GNM vertex attributes support divisors zero and one");
    used_locations |= 1u << attribute.location;
  }
  auto *p = new kgfx_pipeline{};
  p->kind = pipeline_kind;
  p->desc = *x;
  if (!load_shader(p->vs, x->vertex, GNM_SHADER_VERTEX) ||
      !load_shader(p->ps, fragment_variant(x->fragment, x->color_format),
                   GNM_SHADER_PIXEL)) {
    free_resource(p);
    return fail(d, KGFX_INVALID_ARGUMENT, "Invalid native GNM shader artifact");
  }
  const auto *vs = static_cast<const GnmVsShader *>(p->vs.meta.stage);
  const auto *ps = static_cast<const GnmPsShader *>(p->ps.meta.stage);
  p->vsregs = vs->registers;
  p->psregs = ps->registers;
  sceGnmVsRegsSetAddress(&p->vsregs, p->vs.code.mapped);
  sceGnmPsRegsSetAddress(&p->psregs, p->ps.code.mapped);
  if (vs->numinputsemantics) {
    uint32_t remap[KGFX_MAX_VERTEX_ATTRIBUTES]{};
    GnmFetchShaderInstancingMode modes[KGFX_MAX_VERTEX_ATTRIBUTES]{};
    for (uint32_t i = 0; i < KGFX_MAX_VERTEX_ATTRIBUTES; ++i)
      remap[i] = i;
    auto *semantics = sceGnmVsShaderInputSemanticTable(vs);
    for (uint32_t i = 0;
         i < vs->numinputsemantics && i < KGFX_MAX_VERTEX_ATTRIBUTES; ++i) {
      for (uint32_t j = 0; j < x->attribute_count; ++j)
        if (x->attributes[j].location == semantics[i].semantic &&
            x->attributes[j].divisor)
          modes[i] = GNM_FETCH_MODE_INSTANCEID_OVERSTEPRATE0;
    }
    GnmFetchShaderCreateInfo ci{};
    ci.regs = &p->vsregs;
    ci.inputusages = p->vs.meta.inputusageslots;
    ci.numinputusages = p->vs.meta.numinputusageslots;
    ci.vtxinputs = sceGnmVsShaderInputSemanticTable(vs);
    ci.numvtxinputs = vs->numinputsemantics;
    ci.remaptable = remap;
    ci.remaptablecount = KGFX_MAX_VERTEX_ATTRIBUTES;
    ci.instancedata = modes;
    ci.numinstancedata = vs->numinputsemantics;
    uint32_t n = 0;
    GnmFetchShaderResults results{};
    if (vs->numinputsemantics > KGFX_MAX_VERTEX_ATTRIBUTES ||
        sceGnmFetchShaderCalcSize(&n, &ci) != GNM_ERROR_OK ||
        !allocate(p->fetch, n) ||
        sceGnmCreateFetchShader(p->fetch.mapped, n, &ci, &results) !=
            GNM_ERROR_OK) {
      free_resource(p);
      return fail(d, KGFX_DRIVER_ERROR,
                  "Could not generate GNM vertex fetch shader");
    }
    sceGnmVsRegsSetFetchShaderModifier(&p->vsregs, &results);
  }
  p->depth.depthenable = x->depth_test;
  p->depth.zwrite = x->depth_write;
  p->depth.zfunc = compare(x->depth_compare);
  p->raster.cullmode = x->cull == KGFX_CULL_BACK    ? GNM_CULL_BACK
                       : x->cull == KGFX_CULL_FRONT ? GNM_CULL_FRONT
                                                    : GNM_CULL_NONE;
  // GNM defines this before the viewport transform, matching clip-space CCW.
  p->raster.frontface = GNM_FACE_CCW;
  p->raster.frontmode = p->raster.backmode =
      x->wireframe ? GNM_FILL_WIREFRAME : GNM_FILL_SOLID;
  p->blend.blendenabled = x->blend != KGFX_BLEND_OPAQUE;
  p->blend.colorsrcmult = x->blend == KGFX_BLEND_PREMULTIPLIED
                              ? GNM_BLEND_ONE
                              : GNM_BLEND_SRC_ALPHA;
  p->blend.colordstmult = x->blend == KGFX_BLEND_ADDITIVE
                              ? GNM_BLEND_ONE
                              : GNM_BLEND_ONE_MINUS_SRC_ALPHA;
  p->blend.alphasrcmult = GNM_BLEND_ONE;
  p->blend.alphadstmult = x->blend == KGFX_BLEND_ADDITIVE
                              ? GNM_BLEND_ONE
                              : GNM_BLEND_ONE_MINUS_SRC_ALPHA;
  p->blend.separatealphaenable = true;
  register_resource(d, p, pipeline_kind);
  ++d->stats.live_pipelines;
  *out = p;
  return KGFX_OK;
}
void kgfx_pipeline_destroy(kgfx_device *d, kgfx_pipeline *p) {
  if (owned(d, p)) {
    retire(d, p);
  }
}
kgfx_result kgfx_frame_begin(kgfx_device *d) {
  if (!d || d->active)
    return KGFX_INVALID_ARGUMENT;
  d->slot = uint32_t(d->serial % d->frame_count);
  auto &f = d->frames[d->slot];
  auto r = kgfx_wait(d, f.serial, UINT64_MAX);
  if (r != KGFX_OK)
    return r;
  // Submission patches the final flip packet in place. Both its final EOP and
  // presentation must complete before recycling the arena and display buffer.
  if (d->present && (r = wait_flip(d, f.presented)) != KGFX_OK)
    return r;
  collect(d);
  f.used = 0;
  sceGnmCmdReset(&f.cmd);
  d->active = true;
  d->failed = false;
  d->pipeline = nullptr;
  memset(&d->bindings, 0, sizeof(d->bindings));
  sceGnmDrawCmdInitDefaultHardwareState(&f.cmd);
  if (d->present)
    sceGnmDrawCmdWaitUntilSafeForRendering(&f.cmd, d->video.handle, d->slot);
  GnmViewportTransformControl transform{true, true,  true,  true, true,
                                        true, false, false, true};
  sceGnmDrawCmdSetViewportTransformControl(&f.cmd, &transform);
  // Offline vertex specialization already maps GL clip depth into [0,w].
  context_register(d, R_028810_PA_CL_CLIP_CNTL, S_028810_DX_CLIP_SPACE_DEF(1));
  sceGnmDrawCmdSetInstanceStepRate(&f.cmd, 1, 1);
  sceGnmDrawCmdSetPrimitiveType(&f.cmd, GNM_PT_TRILIST);
  return KGFX_OK;
}
kgfx_result kgfx_pass_begin(kgfx_device *d, const kgfx_pass_desc *p) {
  if (!d || !p || !d->active || d->inpass)
    return KGFX_INVALID_ARGUMENT;
  kgfx_texture *color = p->color;
  if (!color && !p->depth && d->present)
    color = &d->display[d->slot];
  if ((color && !owned(d, color)) || (p->depth && !owned(d, p->depth)) ||
      (!color && !p->depth))
    return KGFX_INVALID_ARGUMENT;
  if ((color && (color->desc.usage & KGFX_TEXTURE_CUBE)) ||
      (p->depth && (p->depth->desc.usage & KGFX_TEXTURE_CUBE)))
    return fail(d, KGFX_UNSUPPORTED,
                "Cubemap attachments require an explicit face selector");
  if ((color && color->desc.format == KGFX_DEPTH32F) ||
      (p->depth && p->depth->desc.format != KGFX_DEPTH32F))
    return KGFX_INVALID_ARGUMENT;
  if (color && p->depth &&
      (color->desc.width != p->depth->desc.width ||
       color->desc.height != p->depth->desc.height))
    return fail(d, KGFX_INVALID_ARGUMENT,
                "Pass attachment dimensions must match");
  const auto *attachment = color ? color : p->depth;
  const uint32_t width = p->width ? p->width : attachment->desc.width;
  const uint32_t height = p->height ? p->height : attachment->desc.height;
  if (!room(d))
    return KGFX_OUT_OF_MEMORY;
  const uint32_t samples = color      ? color->desc.samples
                           : p->depth ? p->depth->desc.samples
                                      : 1;
  if (color && p->depth && color->desc.samples != p->depth->desc.samples)
    return fail(d, KGFX_INVALID_ARGUMENT, "Pass sample counts must match");
  multisample_state(d, samples);
  if (color && p->color_load == KGFX_CLEAR)
    clear_color(d, color, p->clear_color);
  if (p->depth && p->depth_load == KGFX_CLEAR) {
    // Depth-only HTILE stores 14-bit min/max and a four-bit sample Z mask.
    // Zero mask selects DB_DEPTH_CLEAR_VALUE without writing every depth
    // sample.
    p->depth->depth_clear = p->clear_depth;
    const uint32_t z =
        uint32_t(fmaxf(0.f, fminf(16383.f, roundf(p->clear_depth * 16383.f))));
    const uint32_t metadata = (z << 18) | (z << 4);
    sceGnmDrawCmdFillMemory(cmd(d),
                            reinterpret_cast<uint64_t>(p->depth->htile.mapped),
                            uint32_t(p->depth->htile_bytes), metadata);
    barrier(d);
    // The DB clear draw establishes depth metadata and depth-cache state.
    sceGnmDrawCmdSetRenderTarget(cmd(d), 0, nullptr);
    sceGnmDrawCmdSetRenderTargetMask(cmd(d), 0);
    sceGnmDrawCmdSetDepthRenderTarget(cmd(d), &p->depth->depth);
    sceGnmDrawCmdSetDepthClearValue(cmd(d), p->clear_depth);
    kgfx_viewport(d, 0, 0, p->depth->desc.width, p->depth->desc.height);
    kgfx_scissor(d, 0, 0, p->depth->desc.width, p->depth->desc.height);
    kgfx_bind_pipeline(d, d->depth_pipeline);
    kgfx_bindings bindings{};
    kgfx_bind_resources(d, &bindings);
    GnmDbRenderControl clear{};
    clear.depthclearenable = true;
    sceGnmDrawCmdSetDbRenderControl(cmd(d), &clear);
    d->inpass = true;
    kgfx_draw(d, 0, 3, 1);
    d->inpass = false;
    barrier(d);
  }
  sceGnmDrawCmdSetRenderTarget(cmd(d), 0, color ? &color->color : nullptr);
  sceGnmDrawCmdSetRenderTargetMask(cmd(d), color ? 15 : 0);
  sceGnmDrawCmdSetDepthRenderTarget(cmd(d),
                                    p->depth ? &p->depth->depth : nullptr);
  GnmDbRenderControl dc{};
  sceGnmDrawCmdSetDbRenderControl(cmd(d), &dc);
  if (p->depth) {
    sceGnmDrawCmdSetDepthClearValue(cmd(d), p->depth->depth_clear);
    p->depth->depth_compressed = true;
  }
  kgfx_viewport(d, 0, 0, width, height);
  kgfx_scissor(d, 0, 0, width, height);
  d->inpass = true;
  return KGFX_OK;
}
void kgfx_pass_end(kgfx_device *d) {
  if (d)
    d->inpass = false;
}
void kgfx_bind_pipeline(kgfx_device *d, kgfx_pipeline *p) {
  if (!owned(d, p) || !room(d))
    return;
  d->pipeline = p;
  for (uint32_t slot = 0; slot < 16; slot += 2) {
    sceGnmDrawCmdSetPointerUserData(cmd(d), GNM_STAGE_VS, slot, nullptr);
    sceGnmDrawCmdSetPointerUserData(cmd(d), GNM_STAGE_PS, slot, nullptr);
  }
  sceGnmDrawCmdSetGuardBands(cmd(d), 32767, 32767, 1, 1);
  sceGnmDrawCmdSetVsShader(cmd(d), &p->vsregs, 0);
  sceGnmDrawCmdSetPsShader(cmd(d), &p->psregs);
  const auto *vs = static_cast<const GnmVsShader *>(p->vs.meta.stage);
  const auto *ps = static_cast<const GnmPsShader *>(p->ps.meta.stage);
  sceGnmDrawCmdSetPsInputUsage(
      cmd(d), sceGnmVsShaderExportSemanticTable(vs), vs->numexportsemantics,
      sceGnmPsShaderInputSemanticTable(ps), ps->numinputsemantics);
  sceGnmDrawCmdSetBlendControl(cmd(d), 0, &p->blend);
  sceGnmDrawCmdSetDepthStencilControl(cmd(d), &p->depth);
  sceGnmDrawCmdSetPrimitiveSetup(cmd(d), &p->raster);
  ++d->stats.pipeline_binds;
}
void kgfx_viewport(kgfx_device *d, int32_t x, int32_t y, uint32_t w,
                   uint32_t h) {
  if (!room(d))
    return;
  GnmSetViewportInfo v{};
  v.dmax = 1;
  v.scale[0] = w * .5f;
  v.scale[1] = -float(h) * .5f;
  v.scale[2] = 1;
  v.offset[0] = x + w * .5f;
  v.offset[1] = y + h * .5f;
  sceGnmDrawCmdSetViewport(cmd(d), 0, &v);
}
void kgfx_scissor(kgfx_device *d, int32_t x, int32_t y, uint32_t w,
                  uint32_t h) {
  if (room(d))
    sceGnmDrawCmdSetScreenScissor(cmd(d), x, y, x + int32_t(w), y + int32_t(h));
}
void kgfx_bind_resources(kgfx_device *d, const kgfx_bindings *b) {
  if (!d || !b || !d->pipeline || !room(d))
    return;
  d->bindings = *b;
  for (auto *v : b->vertex)
    if (v && !prepare_buffer(d, v))
      return;
  for (auto *u : b->uniforms)
    if (u && !prepare_buffer(d, u))
      return;
  if (b->index && !prepare_buffer(d, b->index))
    return;
  auto *p = d->pipeline;
  auto *vertices = static_cast<GnmBuffer *>(
      staging(d, sizeof(GnmBuffer) * KGFX_MAX_VERTEX_ATTRIBUTES));
  auto *table = static_cast<unsigned char *>(staging(d, 64 * 32));
  if (!vertices || !table) {
    d->failed = true;
    return;
  }
  memset(vertices, 0, sizeof(GnmBuffer) * KGFX_MAX_VERTEX_ATTRIBUTES);
  memset(table, 0, 64 * 32);
  for (uint32_t i = 0; i < p->desc.attribute_count; ++i) {
    auto &a = p->desc.attributes[i];
    if (a.buffer_slot > 1 || a.location >= KGFX_MAX_VERTEX_ATTRIBUTES)
      continue;
    auto *v = b->vertex[a.buffer_slot];
    if (!owned(d, v))
      continue;
    uint32_t stride = p->desc.vertex_stride[a.buffer_slot];
    size_t offset = b->vertex_offset[a.buffer_slot] + a.offset;
    if (!stride || offset >= v->size)
      continue;
    GnmDataFormat df = a.type == KGFX_FLOAT2   ? GNM_FMT_R32G32_FLOAT
                       : a.type == KGFX_FLOAT3 ? GNM_FMT_R32G32B32_FLOAT
                       : a.type == KGFX_FLOAT4 ? GNM_FMT_R32G32B32A32_FLOAT
                                               : GNM_FMT_R8G8B8A8_UNORM;
    vertices[a.location] = sceGnmCreateVertexBuffer(
        static_cast<unsigned char *>(v->current) + offset, df, stride,
        uint32_t((v->size - offset + stride - 1) / stride));
  }
  // Shader package binding ABI: 64-byte descriptor slots, constants at binding
  // 0..3 and combined image/sampler records at 8..23. Compiler lowering
  // matches.
  for (uint32_t i = 0; i < KGFX_MAX_UNIFORM_BINDINGS; ++i) {
    auto *u = b->uniforms[i];
    if (!owned(d, u) || b->uniform_offset[i] >= u->size)
      continue;
    size_t bytes = b->uniform_size[i] ? b->uniform_size[i]
                                      : u->size - b->uniform_offset[i];
    auto desc = sceGnmCreateConstBuffer(
        static_cast<unsigned char *>(u->current) + b->uniform_offset[i],
        uint32_t(bytes));
    memcpy(table + i * 64, &desc, sizeof(desc));
  }
  for (uint32_t i = 0; i < KGFX_MAX_TEXTURE_BINDINGS; ++i) {
    if (owned(d, b->textures[i]))
      memcpy(table + (i + 8) * 64, &b->textures[i]->tex, sizeof(GnmTexture));
    if (owned(d, b->samplers[i]))
      memcpy(table + (i + 8) * 64 + 32, &b->samplers[i]->native,
             sizeof(GnmSampler));
  }
  for (unsigned stage = 0; stage < 2; ++stage) {
    auto &s = stage ? p->ps : p->vs;
    auto gstage = stage ? GNM_STAGE_PS : GNM_STAGE_VS;
    for (uint32_t i = 0; i < s.meta.numinputusageslots; ++i) {
      auto &slot = s.meta.inputusageslots[i];
      switch (slot.usagetype) {
      case GNM_SHINPUTUSAGE_SUBPTR_FETCHSHADER:
        sceGnmDrawCmdSetPointerUserData(cmd(d), gstage, slot.startregister,
                                        p->fetch.mapped);
        break;
      case GNM_SHINPUTUSAGE_PTR_VERTEXBUFFERTABLE:
        sceGnmDrawCmdSetPointerUserData(cmd(d), gstage, slot.startregister,
                                        vertices);
        break;
      case GNM_SHINPUTUSAGE_PTR_RESOURCETABLE:
      case GNM_SHINPUTUSAGE_PTR_INDIRECTRESOURCETABLE:
        sceGnmDrawCmdSetPointerUserData(cmd(d), gstage, slot.startregister,
                                        table);
        break;
      default:
        fail(d, KGFX_UNSUPPORTED,
             "Shader requires unsupported user-data binding");
        d->failed = true;
        break;
      }
    }
  }
}
void kgfx_draw(kgfx_device *d, uint32_t first, uint32_t count,
               uint32_t instances) {
  if (!d || !d->inpass || !d->pipeline || !count || !instances || !room(d))
    return;
  context_register(d, R_028408_VGT_INDX_OFFSET, first);
  sceGnmDrawCmdSetNumInstances(cmd(d), instances);
  sceGnmDrawCmdDrawIndexAuto(cmd(d), count);
  ++d->stats.draws;
  d->stats.instances += instances;
}
void kgfx_draw_indexed(kgfx_device *d, uint32_t first, uint32_t count,
                       uint32_t instances) {
  if (!d || !d->inpass || !d->pipeline || !count || !instances || !room(d) ||
      !owned(d, d->bindings.index))
    return;
  auto &b = d->bindings;
  size_t offset = b.index_offset + size_t(first) * b.index_size;
  if ((b.index_size != 2 && b.index_size != 4) || offset > b.index->size ||
      size_t(count) * b.index_size > b.index->size - offset) {
    d->failed = true;
    fail(d, KGFX_INVALID_ARGUMENT, "Index draw exceeds buffer bounds");
    return;
  }
  context_register(d, R_028408_VGT_INDX_OFFSET, 0);
  sceGnmDrawCmdSetNumInstances(cmd(d), instances);
  sceGnmDrawCmdSetIndexSize(
      cmd(d), b.index_size == 2 ? GNM_INDEX_16 : GNM_INDEX_32, GNM_POLICY_LRU);
  sceGnmDrawCmdDrawIndex(
      cmd(d), count, static_cast<unsigned char *>(b.index->current) + offset);
  ++d->stats.draws;
  d->stats.instances += instances;
}
kgfx_result kgfx_texture_transition(kgfx_device *d, kgfx_texture *t,
                                    kgfx_access access) {
  if (!owned(d, t) || !room(d))
    return KGFX_INVALID_ARGUMENT;
  if ((access == KGFX_ACCESS_SAMPLED || access == KGFX_ACCESS_COPY_SOURCE ||
       access == KGFX_ACCESS_COPY_DESTINATION) &&
      !expand_depth(d, t))
    return KGFX_INVALID_ARGUMENT;
  barrier(d);
  return KGFX_OK;
}
kgfx_result kgfx_texture_copy(kgfx_device *d, kgfx_texture *s, kgfx_texture *t,
                              uint32_t sx, uint32_t sy, uint32_t tx,
                              uint32_t ty, uint32_t w, uint32_t h) {
  if (!d || !d->active || d->inpass || !owned(d, s) || !owned(d, t) || s == t ||
      !w || !h || (s->desc.usage & KGFX_TEXTURE_CUBE) ||
      (t->desc.usage & KGFX_TEXTURE_CUBE) ||
      s->desc.samples != t->desc.samples || s->desc.format != t->desc.format ||
      sx > s->desc.width || w > s->desc.width - sx || sy > s->desc.height ||
      h > s->desc.height - sy || tx > t->desc.width || w > t->desc.width - tx ||
      ty > t->desc.height || h > t->desc.height - ty)
    return KGFX_INVALID_ARGUMENT;
  const size_t copy_words = s->desc.format == KGFX_DEPTH32F
                                ? 1024 + 7 * (s->bytes / 0x1ffffc + 1)
                                : 1024;
  if (!room(d, copy_words))
    return KGFX_OUT_OF_MEMORY;
  if (s->desc.format == KGFX_DEPTH32F) {
    if (sx || sy || tx || ty || w != s->desc.width || h != s->desc.height ||
        w != t->desc.width || h != t->desc.height || s->bytes != t->bytes ||
        s->desc.samples != t->desc.samples)
      return fail(d, KGFX_UNSUPPORTED,
                  "Depth copies currently require matching full surfaces");
    if (!expand_depth(d, s) || !expand_depth(d, t))
      return KGFX_INVALID_ARGUMENT;
    barrier(d);
    sceGnmDrawCmdCopyMemory(
        cmd(d), reinterpret_cast<uint64_t>(t->memory.mapped),
        reinterpret_cast<uint64_t>(s->memory.mapped), uint32_t(s->bytes));
    barrier(d);
    return KGFX_OK;
  }
  if (s->desc.samples > 1)
    return fail(d, KGFX_UNSUPPORTED,
                "GNM color rectangle copies require single-sample images");
  barrier(d);
  kgfx_pass_desc pass{};
  pass.color = t;
  pass.width = t->desc.width;
  pass.height = t->desc.height;
  pass.color_load = KGFX_LOAD;
  if (auto result = kgfx_pass_begin(d, &pass); result != KGFX_OK)
    return result;
  kgfx_scissor(d, tx, ty, w, h);
  kgfx_bind_pipeline(d, d->copy_pipeline);
  const int32_t rectangle[] = {int32_t(sx), int32_t(sy), int32_t(tx),
                               int32_t(ty)};
  if (kgfx_buffer_update(d, d->clear_uniform, 0, rectangle,
                         sizeof(rectangle)) != KGFX_OK) {
    kgfx_pass_end(d);
    return KGFX_OUT_OF_MEMORY;
  }
  kgfx_bindings bindings{};
  bindings.uniforms[0] = d->clear_uniform;
  bindings.uniform_size[0] = sizeof(rectangle);
  bindings.textures[0] = s;
  kgfx_bind_resources(d, &bindings);
  kgfx_draw(d, 0, 3, 1);
  kgfx_pass_end(d);
  barrier(d);
  return KGFX_OK;
}
kgfx_result kgfx_texture_resolve(kgfx_device *d, kgfx_texture *s,
                                 kgfx_texture *t) {
  if (!owned(d, s) || !owned(d, t))
    return KGFX_INVALID_ARGUMENT;
  if (s->desc.samples <= 1)
    return kgfx_texture_copy(d, s, t, 0, 0, 0, 0, s->desc.width,
                             s->desc.height);
  if (!d->active || d->inpass || t->desc.samples != 1 ||
      s->desc.width != t->desc.width || s->desc.height != t->desc.height ||
      s->desc.format != t->desc.format || s->desc.format == KGFX_DEPTH32F)
    return KGFX_INVALID_ARGUMENT;
  barrier(d);
  kgfx_pass_desc pass{};
  pass.color = t;
  pass.width = t->desc.width;
  pass.height = t->desc.height;
  pass.color_load = KGFX_DISCARD;
  if (auto result = kgfx_pass_begin(d, &pass); result != KGFX_OK)
    return result;
  kgfx_bind_pipeline(d, s->desc.format == KGFX_RGBA32F ||
                                s->desc.format == KGFX_R32F
                            ? d->resolve_pipeline32
                            : d->resolve_pipeline);
  kgfx_bindings bindings{};
  bindings.textures[0] = s;
  kgfx_bind_resources(d, &bindings);
  kgfx_draw(d, 0, 3, 1);
  kgfx_pass_end(d);
  barrier(d);
  return d->failed ? KGFX_DRIVER_ERROR : KGFX_OK;
}
kgfx_completion kgfx_submit(kgfx_device *d, uint32_t present) {
  if (!d || !d->active || d->inpass)
    return 0;
  if (d->failed) {
    d->active = false;
    return 0;
  }
  auto begin = now_ns();
  auto &f = d->frames[d->slot];
  auto serial = d->serial + 1;
  const bool flip = present && d->present;
  if (size_t(f.cmd.endptr - f.cmd.cmdptr) < (flip ? 64u : 6u)) {
    d->active = false;
    fail(d, KGFX_OUT_OF_MEMORY, "Command arena has no room for submission tail");
    return 0;
  }
  if (flip) {
    // The native driver replaces this mandatory 64-dword final NOP with the
    // VideoOut lock/flip sequence and a trailing completion EOP. Its label is
    // 32-bit; the owning frame restores the public 64-bit serial when polled.
    // An EOP before this packet would allow premature command-arena reuse.
    const auto label = reinterpret_cast<uint64_t>(f.label.mapped);
    memset(f.cmd.cmdptr, 0, 64 * sizeof(uint32_t));
    f.cmd.cmdptr[0] = 0xc03e1000u;
    f.cmd.cmdptr[1] = 0x68750781u; // prepareFlipWithEopInterrupt + label
    f.cmd.cmdptr[2] = uint32_t(label);
    f.cmd.cmdptr[3] = uint32_t(label >> 32);
    f.cmd.cmdptr[4] = uint32_t(serial);
    f.cmd.cmdptr[5] = GNM_CACHE_FLUSH_AND_INV_TS_EVENT;
    f.cmd.cmdptr += 64;
  } else {
    sceGnmDrawCmdEventWriteEop(
        &f.cmd, GNM_CACHE_FLUSH_AND_INV_TS_EVENT,
        reinterpret_cast<uint64_t>(f.label.mapped), GNM_DATA_SEL_SEND_DATA64,
        serial);
    // Offscreen completion is polled and needs no interrupt.
    f.cmd.cmdptr[-3] &= ~(3u << 24);
  }
  uint32_t bytes = uint32_t(reinterpret_cast<uintptr_t>(f.cmd.cmdptr) -
                            reinterpret_cast<uintptr_t>(f.cmd.beginptr));
  void *addresses[] = {f.command.mapped};
  uint32_t sizes[] = {bytes};
  int result =
      flip
          ? sceGnmSubmitAndFlipCommandBuffers(
                1, addresses, sizes, nullptr, nullptr, d->video.handle, d->slot,
                GNM_VIDEO_OUT_FLIP_VSYNC, int64_t(serial))
          : sceGnmSubmitCommandBuffers(1, addresses, sizes, nullptr, nullptr);
  d->active = false;
  if (result < 0) {
    fail(d, KGFX_DRIVER_ERROR, "GNM command submission failed");
    return 0;
  }
  sceGnmSubmitDone();
  d->serial = serial;
  f.serial = serial;
  f.presented = flip ? serial : 0;
  if (flip) {
    d->last_presented = serial;
    ++d->present_count;
  }
  d->stats.frame_serial = serial;
  d->stats.cpu_submit_ms = double(now_ns() - begin) / 1000000;
  return serial;
}
uint32_t kgfx_is_complete(kgfx_device *d, kgfx_completion completion) {
  if (!d)
    return 0;
  for (uint32_t i = 0; i < d->frame_count; ++i) {
    auto &f = d->frames[i];
    if (f.label.mapped && f.serial) {
      const uint64_t value = f.presented
          ? (*static_cast<volatile uint32_t *>(f.label.mapped) == uint32_t(f.serial)
                 ? f.serial : 0)
          : *static_cast<volatile uint64_t *>(f.label.mapped);
      if (value > d->completed)
        d->completed = value;
    }
  }
  d->stats.completed_serial = d->completed;
  return completion <= d->completed;
}
kgfx_result kgfx_wait(kgfx_device *d, kgfx_completion completion,
                      uint64_t timeout) {
  if (!d || completion > d->serial)
    return KGFX_INVALID_ARGUMENT;
  uint64_t start = now_ns();
  while (!kgfx_is_complete(d, completion)) {
    if (timeout != UINT64_MAX && now_ns() - start >= timeout)
      return KGFX_NOT_READY;
    sceKernelUsleep(100);
  }
  return KGFX_OK;
}
kgfx_result kgfx_readback_begin(kgfx_device *d, kgfx_texture *t,
                                kgfx_readback **out) {
  if (!d || !out || !d->active)
    return KGFX_INVALID_ARGUMENT;
  if (!t && d->present)
    t = &d->display[d->slot];
  if (!owned(d, t) || t->desc.samples > 1 ||
      (t->desc.usage & KGFX_TEXTURE_CUBE))
    return KGFX_INVALID_ARGUMENT;
  if (!room(d, 1024 + 7 * (t->bytes / 0x1ffffc + 1)))
    return KGFX_OUT_OF_MEMORY;
  if (!expand_depth(d, t))
    return KGFX_INVALID_ARGUMENT;
  auto *r = new kgfx_readback{};
  if (!allocate(r->memory, t->bytes)) {
    delete r;
    return KGFX_OUT_OF_MEMORY;
  }
  r->bytes = t->bytes;
  r->source_pitch = t->pitch;
  r->display = t->display;
  r->tex = t->tex;
  r->data.width = t->desc.width;
  r->data.height = t->desc.height;
  r->data.format = t->desc.format;
  r->data.row_pitch = size_t(t->desc.width) * pixel_bytes(t->desc.format);
  r->data.size = r->data.row_pitch * t->desc.height;
  r->linear = malloc(r->data.size);
  if (!r->linear) {
    sceGnmDirectMemoryRelease(&r->memory);
    delete r;
    return KGFX_OUT_OF_MEMORY;
  }
  r->data.pixels = r->linear;
  r->ready = d->serial + 1;
  barrier(d);
  // Make render-target writes visible before the DMA read. The EOP dependency
  // also lets emulators materialize their host image before copying guest
  // memory.
  auto fence = reinterpret_cast<uint64_t>(d->frames[d->slot].label.mapped) + 8;
  sceGnmDrawCmdFillMemory(cmd(d), fence, 4, 0);
  sceGnmDrawCmdEventWriteEop(cmd(d), GNM_CACHE_FLUSH_AND_INV_TS_EVENT, fence,
                             GNM_DATA_SEL_SEND_DATA32, 1);
  cmd(d)->cmdptr[-3] &= ~(3u << 24);
  sceGnmDrawCmdWaitMem(cmd(d), GNM_WAIT_REG_MEM_FUNC_EQUAL, fence, 1,
                       0xffffffff);
  sceGnmDrawCmdCopyMemory(cmd(d), reinterpret_cast<uint64_t>(r->memory.mapped),
                          reinterpret_cast<uint64_t>(t->memory.mapped),
                          uint32_t(t->bytes));
  register_resource(d, r, readback_kind);
  d->stats.readback_bytes += r->data.size;
  *out = r;
  return KGFX_OK;
}
kgfx_result kgfx_readback_map(kgfx_device *d, kgfx_readback *r,
                              kgfx_readback_data *out) {
  if (!owned(d, r) || !out)
    return KGFX_INVALID_ARGUMENT;
  if (!kgfx_is_complete(d, r->ready))
    return KGFX_NOT_READY;
  if (r->display) {
    for (uint32_t y = 0; y < r->data.height; ++y)
      memcpy(static_cast<unsigned char *>(r->linear) + y * r->data.row_pitch,
             static_cast<unsigned char *>(r->memory.mapped) +
                 y * r->source_pitch,
             r->data.row_pitch);
  } else {
    auto info = sceGnmTexBuildInfo(&r->tex);
    GpaTilingParams tp{};
    GpaSurfaceContext ctx{};
    if (sceGpaTpInit(&tp, &info, 0, 0) != GPA_ERR_OK ||
        sceGpaInitSurfaceContext(&ctx, r->bytes, &tp) != GPA_ERR_OK)
      return KGFX_DRIVER_ERROR;
    uint32_t bpp = pixel_bytes(r->data.format);
    for (uint32_t y = 0; y < r->data.height; ++y)
      for (uint32_t x = 0; x < r->data.width; ++x) {
        uint64_t at, bit;
        if (sceGpaComputeSurfaceCoord(&at, &bit, &ctx, x, y, 0, 0) !=
                GPA_ERR_OK ||
            at + bpp > r->bytes)
          return KGFX_DRIVER_ERROR;
        memcpy(static_cast<unsigned char *>(r->linear) + y * r->data.row_pitch +
                   x * bpp,
               static_cast<unsigned char *>(r->memory.mapped) + at, bpp);
      }
  }
  *out = r->data;
  return KGFX_OK;
}
void kgfx_readback_destroy(kgfx_device *d, kgfx_readback *r) { retire(d, r); }
void kgfx_get_stats(const kgfx_device *d, kgfx_stats *out) {
  if (d && out)
    *out = d->stats;
}
}
