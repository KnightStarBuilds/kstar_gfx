#include <initializer_list>
#include <kstar_gfx/gfx.h>
#include <kstar_gfx/gnm.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static void log_message(void *, const char *s) { printf("KGFX: %s\n", s); }
static kgfx_shader_artifact load(const char *name) {
  FILE *f = fopen(name, "rb");
  if (!f)
    return {};
  fseek(f, 0, SEEK_END);
  long n = ftell(f);
  rewind(f);
  void *p = malloc(size_t(n));
  if (fread(p, 1, size_t(n), f) != size_t(n)) {
    free(p);
    p = nullptr;
  }
  fclose(f);
  return {p, size_t(n)};
}
// The generated triangle is counter-clockwise in clip-space. This checks the
// public front-face convention independently of depth or model geometry.
static bool culling_check(kgfx_device *d, kgfx_texture *target) {
  auto vs = load("/app0/probe.vert.sb"), ps = load("/app0/probe.frag.sb");
  bool ok = true;
  kgfx_texture *fresh{};
  kgfx_texture_desc td{128, 128, 1, 1, KGFX_TEXTURE_TARGET, KGFX_RGBA8};
  if (kgfx_texture_create(d, &td, &fresh) != KGFX_OK)
    return false;
  for (unsigned iteration = 0; iteration != 3; ++iteration) {
    auto cull = iteration == 0 ? KGFX_CULL_BACK : KGFX_CULL_FRONT;
    kgfx_pipeline_desc pd{};
    pd.vertex = vs;
    pd.fragment = ps;
    pd.color_format = KGFX_RGBA8;
    pd.samples = 1;
    pd.cull = cull;
    kgfx_pipeline *pipeline{};
    if (kgfx_pipeline_create(d, &pd, &pipeline) != KGFX_OK)
      return false;
    kgfx_pass_desc pass{};
    pass.color = iteration == 2 ? fresh : target;
    pass.width = pass.height = 128;
    pass.color_load = KGFX_CLEAR;
    pass.clear_color[3] = 1;
    kgfx_frame_begin(d);
    kgfx_pass_begin(d, &pass);
    kgfx_bind_pipeline(d, pipeline);
    kgfx_bindings bindings{};
    kgfx_bind_resources(d, &bindings);
    kgfx_draw(d, 0, 3, 1);
    kgfx_pass_end(d);
    kgfx_readback *read{};
    kgfx_readback_begin(d, pass.color, &read);
    auto done = kgfx_submit(d, 0);
    if (!done || kgfx_wait(d, done, 10000000000ull) != KGFX_OK)
      return false;
    kgfx_readback_data data{};
    if (kgfx_readback_map(d, read, &data) != KGFX_OK)
      return false;
    auto *pixel = static_cast<const unsigned char *>(data.pixels) +
                  64 * data.row_pitch + 64 * 4;
    bool valid = cull == KGFX_CULL_BACK ? pixel[0] > 240 : pixel[0] < 10;
    printf("KGFX_GPU_CULL_%s_RESULT=%s pixel=%u,%u,%u\n",
           iteration == 2           ? "FRONT_FRESH"
           : cull == KGFX_CULL_BACK ? "BACK"
                                    : "FRONT",
           valid ? "PASS" : "FAIL", pixel[0], pixel[1], pixel[2]);
    ok &= valid;
    kgfx_readback_destroy(d, read);
    kgfx_pipeline_destroy(d, pipeline);
  }
  free(const_cast<void *>(vs.data));
  free(const_cast<void *>(ps.data));
  kgfx_texture_destroy(d, fresh);
  return ok;
}

static bool vertex_abi_checks(kgfx_device *d, kgfx_texture *target) {
  // Nonzero first vertex skips a sentinel. The second stream advances per
  // instance; distinct colors and offsets detect either fetch-rate mistake.
  const float vertices[] = {100, 100,  100, -.8f, -.8f, 0,
                            .8f, -.8f, 0,   0,    .8f,  0};
  const float instances[] = {-.45f, 0, 1, 0, 0, 1, .45f, 0, 0, 1, 0, 1};
  kgfx_buffer *vertex{}, *instance{};
  kgfx_buffer_desc bd{sizeof(vertices), KGFX_BUFFER_VERTEX, vertices};
  if (kgfx_buffer_create(d, &bd, &vertex) != KGFX_OK)
    return false;
  bd = {sizeof(instances), KGFX_BUFFER_VERTEX, instances};
  if (kgfx_buffer_create(d, &bd, &instance) != KGFX_OK)
    return false;
  bool ok = true;
  for (unsigned which = 0; which < 2; ++which) {
    auto vs = load(which ? "/app0/frontface.vert.sb"
                         : "/app0/instance_attributes.vert.sb");
    auto ps =
        load(which ? "/app0/frontface.frag.sb" : "/app0/attributes.frag.sb");
    kgfx_pipeline_desc pd{};
    pd.vertex = vs;
    pd.fragment = ps;
    pd.color_format = KGFX_RGBA8;
    pd.samples = 1;
    if (!which) {
      pd.vertex_stride[0] = 3 * sizeof(float);
      pd.vertex_stride[1] = 6 * sizeof(float);
      pd.attribute_count = 3;
      pd.attributes[0] = {0, 0, 0, 0, KGFX_FLOAT3};
      pd.attributes[1] = {3, 0, 1, 1, KGFX_FLOAT2};
      pd.attributes[2] = {4, 2 * sizeof(float), 1, 1, KGFX_FLOAT4};
    }
    kgfx_pipeline *pipeline{};
    if (kgfx_pipeline_create(d, &pd, &pipeline) != KGFX_OK)
      return false;
    free(const_cast<void *>(vs.data));
    free(const_cast<void *>(ps.data));
    kgfx_pass_desc pass{};
    pass.color = target;
    pass.width = pass.height = 128;
    pass.color_load = KGFX_CLEAR;
    pass.clear_color[3] = 1;
    kgfx_frame_begin(d);
    kgfx_pass_begin(d, &pass);
    kgfx_bind_pipeline(d, pipeline);
    kgfx_bindings bindings{};
    if (!which) {
      bindings.vertex[0] = vertex;
      bindings.vertex[1] = instance;
    }
    kgfx_bind_resources(d, &bindings);
    kgfx_draw(d, which ? 0 : 1, 3, 2);
    kgfx_pass_end(d);
    kgfx_readback *read{};
    kgfx_readback_begin(d, target, &read);
    auto done = kgfx_submit(d, 0);
    if (!done || kgfx_wait(d, done, 10000000000ull) != KGFX_OK)
      return false;
    kgfx_readback_data data{};
    if (kgfx_readback_map(d, read, &data) != KGFX_OK)
      return false;
    auto *pixels = static_cast<const unsigned char *>(data.pixels);
    auto *left = pixels + 64 * data.row_pitch + 35 * 4,
         *right = pixels + 64 * data.row_pitch + 93 * 4;
    bool valid = left[0] > 240 && left[1] < 10 && left[2] < 10 &&
                 right[0] < 10 &&
                 (which ? right[2] > 240 && right[1] < 10
                        : right[1] > 240 && right[2] < 10);
    printf("KGFX_GPU_%s_RESULT=%s left=%u,%u,%u right=%u,%u,%u\n",
           which ? "FRONTFACE" : "INSTANCE_ATTRIBUTES_FIRST_VERTEX",
           valid ? "PASS" : "FAIL", left[0], left[1], left[2], right[0],
           right[1], right[2]);
    ok &= valid;
    kgfx_readback_destroy(d, read);
    kgfx_pipeline_destroy(d, pipeline);
  }
  kgfx_buffer_destroy(d, vertex);
  kgfx_buffer_destroy(d, instance);
  return ok;
}

static bool buffer_overlap_check(kgfx_device *d) {
  // Binding the narrow uniform range before the overlapping vertex span forces
  // cache growth. The second draw reads the migrated range through a fresh bind.
  constexpr size_t buffer_bytes = 256 * 1024, uniform_offset = 64 * 1024;
  auto *initial = static_cast<unsigned char *>(calloc(1, buffer_bytes));
  if (!initial)
    return false;
  const float vertices[] = {-.8f, -.8f, 0, 0, 1, 0, 0, 1,
                            .8f, -.8f, 0, 0, 1, 0, 0, 1,
                            0,    .8f, 0, 0, 1, 0, 0, 1};
  const float params[] = {.5f, 1, 1, 1, .5f, 0, 0, 0};
  memcpy(initial, vertices, sizeof(vertices));
  memcpy(initial + uniform_offset, params, sizeof(params));
  kgfx_buffer_desc bd{buffer_bytes, KGFX_BUFFER_VERTEX | KGFX_BUFFER_UNIFORM,
                      initial};
  kgfx_buffer *shared{};
  auto result = kgfx_buffer_create(d, &bd, &shared);
  free(initial);
  if (result != KGFX_OK)
    return false;
  kgfx_texture_desc td{128, 128, 1, 1, KGFX_TEXTURE_TARGET, KGFX_RGBA8};
  kgfx_texture *target{};
  if (kgfx_texture_create(d, &td, &target) != KGFX_OK)
    return false;
  auto vs = load("/app0/attributes.vert.sb"),
       ps = load("/app0/attributes.frag.sb");
  kgfx_pipeline_desc pd{};
  pd.vertex = vs;
  pd.fragment = ps;
  pd.color_format = KGFX_RGBA8;
  pd.samples = 1;
  pd.vertex_stride[0] = 8 * sizeof(float);
  pd.attribute_count = 2;
  pd.attributes[0] = {0, 0, 0, 0, KGFX_FLOAT3};
  pd.attributes[1] = {3, 4 * sizeof(float), 0, 0, KGFX_FLOAT4};
  kgfx_pipeline *pipeline{};
  result = kgfx_pipeline_create(d, &pd, &pipeline);
  free(const_cast<void *>(vs.data));
  free(const_cast<void *>(ps.data));
  if (result != KGFX_OK)
    return false;
  bool ok = true;
  for (unsigned draw = 0; draw != 2; ++draw) {
    kgfx_frame_begin(d);
    kgfx_pass_desc pass{};
    pass.color = target;
    pass.width = pass.height = 128;
    pass.color_load = KGFX_CLEAR;
    pass.clear_color[0] = .125f;
    pass.clear_color[1] = .25f;
    pass.clear_color[2] = .75f;
    pass.clear_color[3] = 1;
    kgfx_pass_begin(d, &pass);
    kgfx_bind_pipeline(d, pipeline);
    kgfx_bindings bindings{};
    bindings.vertex[0] = shared;
    bindings.uniforms[0] = shared;
    bindings.uniform_offset[0] = uniform_offset;
    bindings.uniform_size[0] = 64 * 1024;
    kgfx_bind_resources(d, &bindings);
    kgfx_draw(d, 0, 3, 2);
    kgfx_pass_end(d);
    kgfx_readback *read{};
    if (kgfx_readback_begin(d, target, &read) != KGFX_OK)
      return false;
    auto done = kgfx_submit(d, 0);
    if (!done || kgfx_wait(d, done, 10000000000ull) != KGFX_OK)
      return false;
    kgfx_readback_data data{};
    if (kgfx_readback_map(d, read, &data) != KGFX_OK)
      return false;
    const auto *pixels = static_cast<const unsigned char *>(data.pixels);
    const auto *left = pixels + 64 * data.row_pitch + 35 * 4;
    const auto *right = pixels + 64 * data.row_pitch + 93 * 4;
    const auto *background = pixels + 4 * data.row_pitch + 4 * 4;
    const bool valid = abs(int(left[0]) - 128) <= 1 && left[1] < 2 &&
                       left[2] < 2 && left[3] == 255 &&
                       abs(int(right[0]) - 128) <= 1 && right[1] < 2 &&
                       right[2] < 2 && right[3] == 255 &&
                       abs(int(background[0]) - 32) <= 1 &&
                       abs(int(background[1]) - 64) <= 1 &&
                       abs(int(background[2]) - 191) <= 1 &&
                       background[3] == 255;
    printf("KGFX_GPU_BUFFER_OVERLAP_RESULT=%s draw=%u left=%u,%u,%u "
           "right=%u,%u,%u background=%u,%u,%u\n",
           valid ? "PASS" : "FAIL", draw, left[0], left[1], left[2],
           right[0], right[1], right[2], background[0], background[1],
           background[2]);
    ok &= valid;
    kgfx_readback_destroy(d, read);
  }
  kgfx_pipeline_destroy(d, pipeline);
  kgfx_texture_destroy(d, target);
  kgfx_buffer_destroy(d, shared);
  return ok;
}

static bool precise_color_checks(kgfx_device *d) {
  auto vs = load("/app0/probe.vert.sb"), ps = load("/app0/precise.frag.sb");
  bool ok = true;
  for (uint32_t samples : {1u, 4u}) {
    kgfx_texture_desc td{128,
                         128,
                         1,
                         samples,
                         KGFX_TEXTURE_TARGET | KGFX_TEXTURE_SAMPLED,
                         KGFX_RGBA32F};
    kgfx_texture *color{}, *resolved{};
    if (kgfx_texture_create(d, &td, &color) != KGFX_OK)
      return false;
    td.samples = 1;
    if (kgfx_texture_create(d, &td, &resolved) != KGFX_OK)
      return false;
    kgfx_pipeline_desc pd{};
    pd.vertex = vs;
    pd.fragment = ps;
    pd.color_format = KGFX_RGBA32F;
    pd.samples = samples;
    kgfx_pipeline *pipeline{};
    if (kgfx_pipeline_create(d, &pd, &pipeline) != KGFX_OK)
      return false;
    kgfx_pass_desc pass{};
    pass.color = color;
    pass.width = pass.height = 128;
    pass.color_load = KGFX_CLEAR;
    kgfx_frame_begin(d);
    kgfx_pass_begin(d, &pass);
    kgfx_bind_pipeline(d, pipeline);
    kgfx_bindings bindings{};
    kgfx_bind_resources(d, &bindings);
    kgfx_draw(d, 0, 3, 1);
    kgfx_pass_end(d);
    if (samples > 1 && kgfx_texture_resolve(d, color, resolved) != KGFX_OK)
      return false;
    kgfx_readback *read{};
    kgfx_readback_begin(d, samples > 1 ? resolved : color, &read);
    auto done = kgfx_submit(d, 0);
    if (!done || kgfx_wait(d, done, 10000000000ull) != KGFX_OK)
      return false;
    kgfx_readback_data data{};
    if (kgfx_readback_map(d, read, &data) != KGFX_OK)
      return false;
    auto *pixel = reinterpret_cast<const float *>(
        static_cast<const unsigned char *>(data.pixels) + 64 * data.row_pitch +
        64 * 16);
    bool valid = fabsf(pixel[0] - 1.0001f) < 1.e-6f &&
                 fabsf(pixel[1] - 12345.125f) < .001f &&
                 fabsf(pixel[2] + .000000123f) < 1.e-10f && pixel[3] == 1.f;
    printf("KGFX_GPU_FP32_%uX_RESULT=%s values=%.9g,%.9g,%.9g,%.9g\n", samples,
           valid ? "PASS" : "FAIL", pixel[0], pixel[1], pixel[2], pixel[3]);
    ok &= valid;
    kgfx_readback_destroy(d, read);
    kgfx_pipeline_destroy(d, pipeline);
    kgfx_texture_destroy(d, color);
    kgfx_texture_destroy(d, resolved);
  }
  free(const_cast<void *>(vs.data));
  free(const_cast<void *>(ps.data));
  return ok;
}

static bool transfer_checks(kgfx_device *d) {
  // One-pixel texture descriptors encode width as zero. They must still use the
  // texture layout, including padded rows, rather than the VideoOut path.
  kgfx_texture_desc td{1, 3, 1, 1, KGFX_TEXTURE_TARGET, KGFX_RGBA32F};
  kgfx_texture *narrow{};
  if (kgfx_texture_create(d, &td, &narrow) != KGFX_OK)
    return false;
  kgfx_pass_desc pass{};
  pass.color = narrow;
  pass.width = 1;
  pass.height = 3;
  pass.color_load = KGFX_CLEAR;
  pass.clear_color[0] = 1.0001f;
  pass.clear_color[1] = 12345.125f;
  pass.clear_color[2] = -.000000123f;
  pass.clear_color[3] = 1.f;
  kgfx_frame_begin(d);
  kgfx_pass_begin(d, &pass);
  kgfx_pass_end(d);
  kgfx_readback *read{};
  kgfx_readback_begin(d, narrow, &read);
  auto done = kgfx_submit(d, 0);
  if (!done || kgfx_wait(d, done, 10000000000ull) != KGFX_OK)
    return false;
  kgfx_readback_data data{};
  if (kgfx_readback_map(d, read, &data) != KGFX_OK)
    return false;
  bool narrow_ok = data.width == 1 && data.height == 3 && data.row_pitch == 16;
  for (unsigned y = 0; y < 3; ++y) {
    auto *pixel = reinterpret_cast<const float *>(
        static_cast<const unsigned char *>(data.pixels) + y * data.row_pitch);
    for (unsigned c = 0; c < 4; ++c)
      narrow_ok &= pixel[c] == pass.clear_color[c];
  }
  printf("KGFX_GPU_NARROW_READBACK_RESULT=%s\n", narrow_ok ? "PASS" : "FAIL");
  kgfx_readback_destroy(d, read);
  kgfx_texture_destroy(d, narrow);

  // A subrectangle copy preserves the untouched border and top-down row order.
  unsigned char source[4 * 4 * 4];
  for (unsigned y = 0; y < 4; ++y)
    for (unsigned x = 0; x < 4; ++x) {
      unsigned n = (y * 4 + x) * 4;
      source[n] = x * 41;
      source[n + 1] = y * 53;
      source[n + 2] = (x + y) * 17;
      source[n + 3] = 255;
    }
  td = {4, 4, 1, 1, KGFX_TEXTURE_SAMPLED, KGFX_RGBA8};
  kgfx_texture *src{}, *dst{};
  if (kgfx_texture_create(d, &td, &src) != KGFX_OK ||
      kgfx_texture_upload(d, src, 0, 0, source, 16) != KGFX_OK)
    return false;
  td.width = 6;
  td.height = 5;
  td.usage = KGFX_TEXTURE_SAMPLED;
  if (kgfx_texture_create(d, &td, &dst) != KGFX_OK)
    return false;
  pass = {};
  pass.color = dst;
  pass.width = 6;
  pass.height = 5;
  pass.color_load = KGFX_CLEAR;
  pass.clear_color[3] = 1;
  kgfx_frame_begin(d);
  kgfx_pass_begin(d, &pass);
  kgfx_pass_end(d);
  if (kgfx_texture_copy(d, src, dst, 1, 1, 2, 2, 2, 2) != KGFX_OK)
    return false;
  kgfx_readback_begin(d, dst, &read);
  done = kgfx_submit(d, 0);
  if (!done || kgfx_wait(d, done, 10000000000ull) != KGFX_OK ||
      kgfx_readback_map(d, read, &data) != KGFX_OK)
    return false;
  bool copy_ok = true;
  for (unsigned y = 0; y < 5; ++y)
    for (unsigned x = 0; x < 6; ++x) {
      auto *pixel = static_cast<const unsigned char *>(data.pixels) +
                    y * data.row_pitch + x * 4;
      const unsigned char border[] = {0, 0, 0, 255};
      auto *expected = x >= 2 && x < 4 && y >= 2 && y < 4
                           ? source + ((y - 1) * 4 + x - 1) * 4
                           : border;
      copy_ok &= memcmp(pixel, expected, 4) == 0;
    }
  printf("KGFX_GPU_RECT_COPY_RESULT=%s\n", copy_ok ? "PASS" : "FAIL");
  kgfx_readback_destroy(d, read);
  kgfx_texture_destroy(d, src);
  kgfx_texture_destroy(d, dst);
  return narrow_ok && copy_ok;
}

static bool hdr_blend_checks(kgfx_device *d) {
  auto vs = load("/app0/attributes.vert.sb"),
       ps = load("/app0/attributes.frag.sb");
  const float vertices[] = {-.8f, -.8f, 0, 1, 1,   1, 1, .8f, -.8f, 0, 1,
                            1,    1,    1, 0, .8f, 0, 1, 1,   1,    1};
  const float params[] = {2, 0, 0, .25f, .2f, 0, 0, 0};
  kgfx_buffer *vertex{}, *uniform{};
  kgfx_buffer_desc bd{sizeof(vertices), KGFX_BUFFER_VERTEX, vertices};
  if (kgfx_buffer_create(d, &bd, &vertex) != KGFX_OK)
    return false;
  bd = {sizeof(params), KGFX_BUFFER_UNIFORM, params};
  if (kgfx_buffer_create(d, &bd, &uniform) != KGFX_OK)
    return false;
  kgfx_texture_desc td{
      128, 128, 1, 4, KGFX_TEXTURE_TARGET | KGFX_TEXTURE_SAMPLED, KGFX_RGBA16F};
  kgfx_texture *color{}, *resolved{}, *depth{};
  if (kgfx_texture_create(d, &td, &color) != KGFX_OK)
    return false;
  td.samples = 1;
  if (kgfx_texture_create(d, &td, &resolved) != KGFX_OK)
    return false;
  td.samples = 4;
  td.format = KGFX_DEPTH32F;
  if (kgfx_texture_create(d, &td, &depth) != KGFX_OK)
    return false;
  bool ok = true;
  for (auto mode : {KGFX_BLEND_ALPHA, KGFX_BLEND_ADDITIVE}) {
    kgfx_pipeline_desc pd{};
    pd.vertex = vs;
    pd.fragment = ps;
    pd.color_format = KGFX_RGBA16F;
    pd.samples = 4;
    pd.depth_format = KGFX_DEPTH32F;
    pd.depth_test = 1;
    pd.depth_write = 0;
    pd.depth_compare = KGFX_COMPARE_LESS;
    pd.blend = mode;
    pd.vertex_stride[0] = 7 * sizeof(float);
    pd.attribute_count = 2;
    pd.attributes[0] = {0, 0, 0, 0, KGFX_FLOAT3};
    pd.attributes[1] = {3, 3 * sizeof(float), 0, 0, KGFX_FLOAT4};
    kgfx_pipeline *pipeline{};
    if (kgfx_pipeline_create(d, &pd, &pipeline) != KGFX_OK)
      return false;
    kgfx_pass_desc pass{};
    pass.color = color;
    pass.depth = depth;
    pass.width = pass.height = 128;
    pass.color_load = pass.depth_load = KGFX_CLEAR;
    pass.clear_depth = 1;
    pass.clear_color[2] = 3;
    pass.clear_color[3] = 1;
    kgfx_frame_begin(d);
    kgfx_pass_begin(d, &pass);
    kgfx_bind_pipeline(d, pipeline);
    kgfx_bindings bindings{};
    bindings.vertex[0] = vertex;
    bindings.uniforms[0] = uniform;
    bindings.uniform_size[0] = sizeof(params);
    kgfx_bind_resources(d, &bindings);
    kgfx_draw(d, 0, 3, 1);
    kgfx_pass_end(d);
    if (kgfx_texture_resolve(d, color, resolved) != KGFX_OK)
      return false;
    kgfx_readback *read{};
    kgfx_readback_begin(d, resolved, &read);
    auto done = kgfx_submit(d, 0);
    if (!done || kgfx_wait(d, done, 10000000000ull) != KGFX_OK)
      return false;
    kgfx_readback_data data{};
    if (kgfx_readback_map(d, read, &data) != KGFX_OK)
      return false;
    auto *pixel = reinterpret_cast<const uint16_t *>(
        static_cast<const unsigned char *>(data.pixels) + 64 * data.row_pitch +
        35 * 8);
    bool valid = pixel[0] == 0x3800 && pixel[1] == 0 &&
                 pixel[2] == (mode == KGFX_BLEND_ALPHA ? 0x4080 : 0x4200) &&
                 pixel[3] == (mode == KGFX_BLEND_ALPHA ? 0x3c00 : 0x3d00);
    printf("KGFX_GPU_HDR_%s_4X_RESULT=%s half=%x,%x,%x,%x\n",
           mode == KGFX_BLEND_ALPHA ? "ALPHA" : "ADDITIVE",
           valid ? "PASS" : "FAIL", pixel[0], pixel[1], pixel[2], pixel[3]);
    ok &= valid;
    kgfx_readback_destroy(d, read);
    kgfx_pipeline_destroy(d, pipeline);
  }
  free(const_cast<void *>(vs.data));
  free(const_cast<void *>(ps.data));
  kgfx_texture_destroy(d, color);
  kgfx_texture_destroy(d, resolved);
  kgfx_texture_destroy(d, depth);
  kgfx_buffer_destroy(d, vertex);
  kgfx_buffer_destroy(d, uniform);
  return ok;
}

static bool discard_depth_check(kgfx_device *d) {
  auto vs = load("/app0/attributes.vert.sb"),
       ps = load("/app0/discard.frag.sb");
  kgfx_pipeline_desc pd{};
  pd.vertex = vs;
  pd.fragment = ps;
  pd.samples = 1;
  pd.color_format = KGFX_RGBA8;
  pd.depth_format = KGFX_DEPTH32F;
  pd.depth_test = pd.depth_write = 1;
  pd.depth_compare = KGFX_COMPARE_LESS;
  pd.vertex_stride[0] = 7 * sizeof(float);
  pd.attribute_count = 2;
  pd.attributes[0] = {0, 0, 0, 0, KGFX_FLOAT3};
  pd.attributes[1] = {3, 3 * sizeof(float), 0, 0, KGFX_FLOAT4};
  kgfx_pipeline *discard_pipeline{};
  if (kgfx_pipeline_create(d, &pd, &discard_pipeline) != KGFX_OK)
    return false;
  free(const_cast<void *>(vs.data));
  free(const_cast<void *>(ps.data));
  vs = load("/app0/probe.vert.sb");
  ps = load("/app0/depth_sample.frag.sb");
  pd = {};
  pd.vertex = vs;
  pd.fragment = ps;
  pd.samples = 1;
  pd.color_format = KGFX_RGBA8;
  kgfx_pipeline *sample_pipeline{};
  if (kgfx_pipeline_create(d, &pd, &sample_pipeline) != KGFX_OK)
    return false;
  free(const_cast<void *>(vs.data));
  free(const_cast<void *>(ps.data));
  const float vertices[] = {-.8f, -.8f, 0, 1, 1,   1, 1, .8f, -.8f, 0, 1,
                            1,    1,    1, 0, .8f, 0, 1, 1,   1,    1};
  float params[] = {1, 1, 1, .28f, .2f, 0, 0, 0};
  kgfx_buffer *vertex{}, *uniform{};
  kgfx_buffer_desc bd{sizeof(vertices), KGFX_BUFFER_VERTEX, vertices};
  if (kgfx_buffer_create(d, &bd, &vertex) != KGFX_OK)
    return false;
  bd = {sizeof(params), KGFX_BUFFER_UNIFORM | KGFX_BUFFER_DYNAMIC, params};
  if (kgfx_buffer_create(d, &bd, &uniform) != KGFX_OK)
    return false;
  kgfx_texture_desc td{
      128,          128, 1, 1, KGFX_TEXTURE_TARGET | KGFX_TEXTURE_SAMPLED,
      KGFX_DEPTH32F};
  kgfx_texture *depth{}, *color{};
  if (kgfx_texture_create(d, &td, &depth) != KGFX_OK)
    return false;
  td.format = KGFX_RGBA8;
  if (kgfx_texture_create(d, &td, &color) != KGFX_OK)
    return false;
  kgfx_sampler_desc sd{};
  sd.min_filter = sd.mag_filter = KGFX_NEAREST;
  kgfx_sampler *sampler{};
  if (kgfx_sampler_create(d, &sd, &sampler) != KGFX_OK)
    return false;
  bool ok = true;
  for (unsigned which = 0; which < 2; ++which) {
    kgfx_frame_begin(d);
    params[3] = which ? .5f : .28f;
    kgfx_buffer_update(d, uniform, 0, params, sizeof(params));
    kgfx_pass_desc pass{};
    pass.depth = depth;
    pass.width = pass.height = 128;
    pass.depth_load = KGFX_CLEAR;
    pass.clear_depth = 1;
    kgfx_pass_begin(d, &pass);
    kgfx_bind_pipeline(d, discard_pipeline);
    kgfx_bindings bindings{};
    bindings.vertex[0] = vertex;
    bindings.uniforms[0] = uniform;
    bindings.uniform_size[0] = sizeof(params);
    kgfx_bind_resources(d, &bindings);
    kgfx_draw(d, 0, 3, 1);
    kgfx_pass_end(d);
    if (kgfx_texture_transition(d, depth, KGFX_ACCESS_SAMPLED) != KGFX_OK)
      return false;
    pass = {};
    pass.color = color;
    pass.width = pass.height = 128;
    pass.color_load = KGFX_CLEAR;
    kgfx_pass_begin(d, &pass);
    kgfx_bind_pipeline(d, sample_pipeline);
    bindings = {};
    bindings.textures[0] = depth;
    bindings.samplers[0] = sampler;
    kgfx_bind_resources(d, &bindings);
    kgfx_draw(d, 0, 3, 1);
    kgfx_pass_end(d);
    kgfx_readback *read{};
    kgfx_readback_begin(d, color, &read);
    auto done = kgfx_submit(d, 0);
    if (!done || kgfx_wait(d, done, 10000000000ull) != KGFX_OK)
      return false;
    kgfx_readback_data data{};
    if (kgfx_readback_map(d, read, &data) != KGFX_OK)
      return false;
    auto *pixel = static_cast<const unsigned char *>(data.pixels) +
                  64 * data.row_pitch + 64 * 4;
    bool valid = abs(int(pixel[0]) - (which ? 51 : 255)) <= 1;
    printf("KGFX_GPU_DISCARD_%s_RESULT=%s depth=%u\n", which ? "KEEP" : "KILL",
           valid ? "PASS" : "FAIL", pixel[0]);
    ok &= valid;
    kgfx_readback_destroy(d, read);
  }
  kgfx_pipeline_destroy(d, discard_pipeline);
  kgfx_pipeline_destroy(d, sample_pipeline);
  kgfx_texture_destroy(d, depth);
  kgfx_texture_destroy(d, color);
  kgfx_sampler_destroy(d, sampler);
  kgfx_buffer_destroy(d, vertex);
  kgfx_buffer_destroy(d, uniform);
  return ok;
}

static bool attachment_storage_checks(kgfx_device *d, kgfx_texture *output) {
  // Three levels exceed the allocation of the base 256-square attachment.
  kgfx_texture_desc td{
      256, 256, 3, 1, KGFX_TEXTURE_TARGET | KGFX_TEXTURE_SAMPLED, KGFX_RGBA8};
  kgfx_texture *target{}, *depth{}, *cube{};
  if (kgfx_texture_create(d, &td, &target) != KGFX_OK)
    return false;
  for (unsigned level = 0; level < 3; ++level) {
    unsigned width = 256 >> level;
    auto *pixels = static_cast<unsigned char *>(malloc(width * width * 4));
    if (!pixels)
      return false;
    for (unsigned i = 0; i < width * width; ++i) {
      pixels[i * 4] = level ? 0 : 17;
      pixels[i * 4 + 1] = level ? 255 : 34;
      pixels[i * 4 + 2] = level ? 0 : 51;
      pixels[i * 4 + 3] = 255;
    }
    auto result = kgfx_texture_upload(d, target, level, 0, pixels, width * 4);
    free(pixels);
    if (result != KGFX_OK)
      return false;
  }
  td = {128, 128, 1, 1, KGFX_TEXTURE_TARGET, KGFX_DEPTH32F};
  if (kgfx_texture_create(d, &td, &depth) != KGFX_OK)
    return false;
  td = {8, 8, 1, 1, KGFX_TEXTURE_CUBE | KGFX_TEXTURE_SAMPLED, KGFX_RGBA8};
  if (kgfx_texture_create(d, &td, &cube) != KGFX_OK)
    return false;
  auto vs = load("/app0/probe.vert.sb"), ps = load("/app0/probe.frag.sb");
  kgfx_pipeline_desc pd{};
  pd.vertex = vs;
  pd.fragment = ps;
  pd.color_format = KGFX_RGBA8;
  pd.samples = 1;
  kgfx_pipeline *pipeline{};
  if (kgfx_pipeline_create(d, &pd, &pipeline) != KGFX_OK)
    return false;
  free(const_cast<void *>(vs.data));
  free(const_cast<void *>(ps.data));
  kgfx_frame_begin(d);
  kgfx_pass_desc pass{};
  pass.color = depth;
  bool contracts = kgfx_pass_begin(d, &pass) == KGFX_INVALID_ARGUMENT;
  pass.color = target;
  pass.depth = target;
  contracts &= kgfx_pass_begin(d, &pass) == KGFX_INVALID_ARGUMENT;
  pass.depth = depth;
  contracts &= kgfx_pass_begin(d, &pass) == KGFX_INVALID_ARGUMENT;
  pass.color = cube;
  pass.depth = nullptr;
  contracts &= kgfx_pass_begin(d, &pass) == KGFX_UNSUPPORTED;
  kgfx_readback *invalid_read{};
  contracts &=
      kgfx_readback_begin(d, cube, &invalid_read) == KGFX_INVALID_ARGUMENT;
  if (invalid_read)
    kgfx_readback_destroy(d, invalid_read);
  printf("KGFX_GPU_ATTACHMENT_CONTRACT_RESULT=%s\n",
         contracts ? "PASS" : "FAIL");
  pass = {};
  pass.color = target;
  pass.color_load = KGFX_LOAD;
  // Zero dimensions must infer the target extent before viewport/scissor setup.
  if (kgfx_pass_begin(d, &pass) != KGFX_OK)
    return false;
  kgfx_bind_pipeline(d, pipeline);
  kgfx_bindings bindings{};
  kgfx_bind_resources(d, &bindings);
  kgfx_draw(d, 0, 3, 1);
  kgfx_pass_end(d);
  kgfx_readback *read{};
  kgfx_readback_begin(d, target, &read);
  auto done = kgfx_submit(d, 0);
  if (!done || kgfx_wait(d, done, 10000000000ull) != KGFX_OK)
    return false;
  kgfx_readback_data data{};
  if (kgfx_readback_map(d, read, &data) != KGFX_OK)
    return false;
  auto *pixels = static_cast<const unsigned char *>(data.pixels);
  auto *center = pixels + 128 * data.row_pitch + 128 * 4;
  bool base = data.width == 256 && data.height == 256 && center[0] == 255 &&
              center[1] == 0 && center[2] == 0 && pixels[0] == 17 &&
              pixels[1] == 34 && pixels[2] == 51;
  printf("KGFX_GPU_MIP_TARGET_BASE_RESULT=%s center=%u,%u,%u corner=%u,%u,%u\n",
         base ? "PASS" : "FAIL", center[0], center[1], center[2], pixels[0],
         pixels[1], pixels[2]);
  kgfx_readback_destroy(d, read);
  kgfx_pipeline_destroy(d, pipeline);
  vs = load("/app0/probe.vert.sb");
  ps = load("/app0/mip_sample.frag.sb");
  pd.vertex = vs;
  pd.fragment = ps;
  if (kgfx_pipeline_create(d, &pd, &pipeline) != KGFX_OK)
    return false;
  free(const_cast<void *>(vs.data));
  free(const_cast<void *>(ps.data));
  kgfx_sampler_desc sd{};
  sd.min_filter = sd.mag_filter = KGFX_NEAREST;
  sd.mipmapped = 1;
  kgfx_sampler *sampler{};
  kgfx_sampler_create(d, &sd, &sampler);
  kgfx_frame_begin(d);
  pass = {};
  pass.color = output;
  pass.color_load = KGFX_CLEAR;
  kgfx_pass_begin(d, &pass);
  kgfx_bind_pipeline(d, pipeline);
  bindings = {};
  bindings.textures[0] = target;
  bindings.samplers[0] = sampler;
  kgfx_bind_resources(d, &bindings);
  kgfx_draw(d, 0, 3, 1);
  kgfx_pass_end(d);
  kgfx_readback_begin(d, output, &read);
  done = kgfx_submit(d, 0);
  if (!done || kgfx_wait(d, done, 10000000000ull) != KGFX_OK ||
      kgfx_readback_map(d, read, &data) != KGFX_OK)
    return false;
  center = static_cast<const unsigned char *>(data.pixels) +
           64 * data.row_pitch + 64 * 4;
  bool mip = center[0] == 0 && center[1] == 255 && center[2] == 0;
  printf("KGFX_GPU_MIP_TARGET_SAMPLE_RESULT=%s pixel=%u,%u,%u\n",
         mip ? "PASS" : "FAIL", center[0], center[1], center[2]);
  kgfx_readback_destroy(d, read);
  kgfx_pipeline_destroy(d, pipeline);
  kgfx_sampler_destroy(d, sampler);
  kgfx_texture_destroy(d, target);
  kgfx_texture_destroy(d, depth);
  kgfx_texture_destroy(d, cube);
  return contracts && base && mip;
}

static bool hdr_msaa_check(kgfx_device *d) {
  kgfx_texture_desc td{
      320, 180, 1, 4, KGFX_TEXTURE_TARGET | KGFX_TEXTURE_SAMPLED, KGFX_RGBA16F};
  kgfx_texture *msaa{}, *resolved{};
  if (kgfx_texture_create(d, &td, &msaa) != KGFX_OK)
    return false;
  td.samples = 1;
  if (kgfx_texture_create(d, &td, &resolved) != KGFX_OK)
    return false;
  auto vs = load("/app0/probe.vert.sb"), ps = load("/app0/probe.frag.sb");
  kgfx_pipeline_desc pd{};
  pd.vertex = vs;
  pd.fragment = ps;
  pd.color_format = KGFX_RGBA16F;
  pd.samples = 4;
  kgfx_pipeline *pipeline{};
  if (kgfx_pipeline_create(d, &pd, &pipeline) != KGFX_OK)
    return false;
  free(const_cast<void *>(vs.data));
  free(const_cast<void *>(ps.data));
  kgfx_pass_desc pass{};
  pass.color = msaa;
  pass.width = 320;
  pass.height = 180;
  pass.color_load = KGFX_CLEAR;
  pass.clear_color[3] = 1;
  kgfx_frame_begin(d);
  kgfx_pass_begin(d, &pass);
  kgfx_bind_pipeline(d, pipeline);
  kgfx_bindings bindings{};
  kgfx_bind_resources(d, &bindings);
  kgfx_draw(d, 0, 3, 1);
  kgfx_pass_end(d);
  if (kgfx_texture_resolve(d, msaa, resolved) != KGFX_OK)
    return false;
  kgfx_readback *read{};
  kgfx_readback_begin(d, resolved, &read);
  auto done = kgfx_submit(d, 0);
  if (!done || kgfx_wait(d, done, 10000000000ull) != KGFX_OK)
    return false;
  kgfx_readback_data data{};
  if (kgfx_readback_map(d, read, &data) != KGFX_OK)
    return false;
  auto *center = reinterpret_cast<const uint16_t *>(
      static_cast<const unsigned char *>(data.pixels) + 90 * data.row_pitch +
      160 * 8);
  unsigned partial = 0;
  for (unsigned y = 0; y < 180; ++y)
    for (unsigned x = 0; x < 320; ++x) {
      auto *p = reinterpret_cast<const uint16_t *>(
          static_cast<const unsigned char *>(data.pixels) + y * data.row_pitch +
          x * 8);
      if (p[0] > 0 && p[0] < 0x3c00)
        ++partial;
    }
  bool ok =
      center[0] == 0x3c00 && center[1] == 0 && center[2] == 0 && partial > 20;
  printf("KGFX_GPU_HDR_MSAA_RESULT=%s centerhalf=%x,%x,%x partial=%u\n",
         ok ? "PASS" : "FAIL", center[0], center[1], center[2], partial);
  kgfx_readback_destroy(d, read);
  kgfx_pipeline_destroy(d, pipeline);
  kgfx_texture_destroy(d, msaa);
  kgfx_texture_destroy(d, resolved);
  return ok;
}

static bool feature_checks(kgfx_device *d, kgfx_texture *target) {
  auto vs = load("/app0/attributes.vert.sb"),
       ps = load("/app0/attributes.frag.sb");
  kgfx_pipeline_desc pd{};
  pd.vertex = vs;
  pd.fragment = ps;
  pd.color_format = KGFX_RGBA8;
  pd.depth_format = KGFX_DEPTH32F;
  pd.samples = 1;
  pd.depth_test = 1;
  pd.depth_write = 1;
  pd.depth_compare = KGFX_COMPARE_LESS;
  pd.vertex_stride[0] = 7 * sizeof(float);
  pd.attribute_count = 2;
  pd.attributes[0] = {0, 0, 0, 0, KGFX_FLOAT3};
  pd.attributes[1] = {3, 3 * sizeof(float), 0, 0, KGFX_FLOAT4};
  kgfx_pipeline *opaque{}, *alpha{};
  if (kgfx_pipeline_create(d, &pd, &opaque) != KGFX_OK)
    return false;
  pd.blend = KGFX_BLEND_ALPHA;
  pd.depth_write = 0;
  if (kgfx_pipeline_create(d, &pd, &alpha) != KGFX_OK)
    return false;
  free(const_cast<void *>(vs.data));
  free(const_cast<void *>(ps.data));
  const float vertices[] = {100, 100, 100, 0, 0,   0,    0, -.8f, -.8f, 0,
                            1,   1,   1,   1, .8f, -.8f, 0, 1,    1,    1,
                            1,   0,   .8f, 0, 1,   1,    1, 1};
  const uint16_t indices[] = {1, 2, 3};
  float params[] = {1, 0, 0, 1, .2f, 0, 0, 0};
  kgfx_buffer *vertex{}, *index{}, *uniform{};
  kgfx_buffer_desc bd{sizeof(vertices), KGFX_BUFFER_VERTEX, vertices};
  kgfx_buffer_create(d, &bd, &vertex);
  bd = {sizeof(indices), KGFX_BUFFER_INDEX, indices};
  kgfx_buffer_create(d, &bd, &index);
  bd = {sizeof(params), KGFX_BUFFER_UNIFORM | KGFX_BUFFER_DYNAMIC, params};
  kgfx_buffer_create(d, &bd, &uniform);
  kgfx_texture *depth{};
  kgfx_texture_desc td{
      128,          128, 1, 1, KGFX_TEXTURE_TARGET | KGFX_TEXTURE_SAMPLED,
      KGFX_DEPTH32F};
  if (kgfx_texture_create(d, &td, &depth) != KGFX_OK)
    return false;
  kgfx_bindings binding{};
  binding.vertex[0] = vertex;
  binding.index = index;
  binding.index_size = 2;
  binding.uniforms[0] = uniform;
  binding.uniform_size[0] = sizeof(params);
  kgfx_pass_desc pass{};
  pass.color = target;
  pass.depth = depth;
  pass.width = pass.height = 128;
  pass.color_load = pass.depth_load = KGFX_CLEAR;
  pass.clear_depth = 1;
  pass.clear_color[3] = 1;
  kgfx_frame_begin(d);
  kgfx_pass_begin(d, &pass);
  kgfx_bind_pipeline(d, opaque);
  kgfx_bind_resources(d, &binding);
  kgfx_draw_indexed(d, 0, 3, 2);
  params[0] = 0;
  params[2] = 1;
  params[4] = .8f;
  kgfx_buffer_update(d, uniform, 0, params, sizeof(params));
  kgfx_bind_resources(d, &binding);
  kgfx_draw_indexed(d, 0, 3, 2);
  kgfx_pass_end(d);
  kgfx_readback *read{};
  kgfx_readback_begin(d, target, &read);
  auto done = kgfx_submit(d, 0);
  if (!done || kgfx_wait(d, done, 10000000000ull) != KGFX_OK)
    return false;
  kgfx_readback_data data{};
  if (kgfx_readback_map(d, read, &data) != KGFX_OK)
    return false;
  auto pixels = static_cast<const unsigned char *>(data.pixels);
  auto a = pixels + 64 * data.row_pitch + 35 * 4,
       b = pixels + 64 * data.row_pitch + 93 * 4;
  bool depthok = a[0] > 240 && a[2] < 10 && b[0] > 240 && b[2] < 10;
  printf(
      "KGFX_GPU_DEPTH_VERTEX_INSTANCE_RESULT=%s left=%u,%u,%u right=%u,%u,%u\n",
      depthok ? "PASS" : "FAIL", a[0], a[1], a[2], b[0], b[1], b[2]);
  kgfx_readback_destroy(d, read);
  kgfx_texture *depth_copy{};
  if (kgfx_texture_create(d, &td, &depth_copy) != KGFX_OK)
    return false;
  auto sample_vs = load("/app0/probe.vert.sb"),
       sample_ps = load("/app0/depth_sample.frag.sb");
  kgfx_pipeline_desc sample_desc{};
  sample_desc.vertex = sample_vs;
  sample_desc.fragment = sample_ps;
  sample_desc.color_format = KGFX_RGBA8;
  sample_desc.samples = 1;
  kgfx_pipeline *sample_pipeline{};
  if (kgfx_pipeline_create(d, &sample_desc, &sample_pipeline) != KGFX_OK)
    return false;
  free(const_cast<void *>(sample_vs.data));
  free(const_cast<void *>(sample_ps.data));
  kgfx_sampler_desc sample_state{};
  sample_state.min_filter = sample_state.mag_filter = KGFX_NEAREST;
  kgfx_sampler *depth_sampler{};
  kgfx_sampler_create(d, &sample_state, &depth_sampler);
  kgfx_readback *depth_reads[2]{};
  kgfx_texture *depth_sources[] = {depth, depth_copy};
  kgfx_frame_begin(d);
  if (kgfx_texture_transition(d, depth, KGFX_ACCESS_SAMPLED) != KGFX_OK)
    return false;
  for (unsigned which = 0; which < 2; ++which) {
    if (which && kgfx_texture_copy(d, depth, depth_copy, 0, 0, 0, 0, 128,
                                   128) != KGFX_OK)
      return false;
    kgfx_pass_desc sp{};
    sp.color = target;
    sp.width = sp.height = 128;
    sp.color_load = KGFX_CLEAR;
    kgfx_pass_begin(d, &sp);
    kgfx_bind_pipeline(d, sample_pipeline);
    kgfx_bindings sb{};
    sb.textures[0] = depth_sources[which];
    sb.samplers[0] = depth_sampler;
    kgfx_bind_resources(d, &sb);
    kgfx_draw(d, 0, 3, 1);
    kgfx_pass_end(d);
    kgfx_readback_begin(d, target, &depth_reads[which]);
  }
  done = kgfx_submit(d, 0);
  if (!done || kgfx_wait(d, done, 10000000000ull) != KGFX_OK)
    return false;
  bool sample_ok = true;
  for (unsigned which = 0; which < 2; ++which) {
    if (kgfx_readback_map(d, depth_reads[which], &data) != KGFX_OK)
      return false;
    auto *pixel = static_cast<const unsigned char *>(data.pixels) +
                  64 * data.row_pitch + 64 * 4;
    bool valid = pixel[0] >= 50 && pixel[0] <= 52;
    sample_ok &= valid;
    printf("KGFX_GPU_DEPTH_%s_RESULT=%s pixel=%u\n", which ? "COPY" : "SAMPLE",
           valid ? "PASS" : "FAIL", pixel[0]);
    kgfx_readback_destroy(d, depth_reads[which]);
  }
  kgfx_pipeline_destroy(d, sample_pipeline);
  kgfx_sampler_destroy(d, depth_sampler);
  kgfx_texture_destroy(d, depth_copy);
  params[0] = 1;
  params[2] = 0;
  params[3] = .5f;
  params[4] = .2f;
  pass.clear_color[2] = 1;
  kgfx_frame_begin(d);
  kgfx_buffer_update(d, uniform, 0, params, sizeof(params));
  kgfx_pass_begin(d, &pass);
  kgfx_bind_pipeline(d, alpha);
  kgfx_bind_resources(d, &binding);
  kgfx_draw_indexed(d, 0, 3, 2);
  kgfx_pass_end(d);
  kgfx_readback_begin(d, target, &read);
  done = kgfx_submit(d, 0);
  if (!done || kgfx_wait(d, done, 10000000000ull) != KGFX_OK)
    return false;
  if (kgfx_readback_map(d, read, &data) != KGFX_OK)
    return false;
  a = static_cast<const unsigned char *>(data.pixels) + 64 * data.row_pitch +
      35 * 4;
  bool blendok =
      a[0] >= 126 && a[0] <= 129 && a[1] < 10 && a[2] >= 126 && a[2] <= 129;
  printf("KGFX_GPU_BLEND_RESULT=%s pixel=%u,%u,%u\n", blendok ? "PASS" : "FAIL",
         a[0], a[1], a[2]);
  kgfx_readback_destroy(d, read);
  kgfx_pipeline_destroy(d, opaque);
  kgfx_pipeline_destroy(d, alpha);
  kgfx_texture_destroy(d, depth);
  kgfx_buffer_destroy(d, vertex);
  kgfx_buffer_destroy(d, index);
  kgfx_buffer_destroy(d, uniform);
  kgfx_texture *cube{}, *hdr{};
  td = {4, 4, 2, 1, KGFX_TEXTURE_SAMPLED | KGFX_TEXTURE_CUBE, KGFX_RGBA32F};
  if (kgfx_texture_create(d, &td, &cube) != KGFX_OK)
    return false;
  for (unsigned face = 0; face < 6; ++face)
    for (unsigned mip = 0; mip < 2; ++mip) {
      float texels[64];
      unsigned n = 4 >> mip;
      for (unsigned i = 0; i < n * n; ++i) {
        texels[4 * i] = (mip ? 0 : 2);
        texels[4 * i + 1] = (mip ? 3 : 0);
        texels[4 * i + 2] = 0;
        texels[4 * i + 3] = 1;
      }
      if (kgfx_texture_upload(d, cube, mip, face, texels, n * 16) != KGFX_OK) {
        printf("KGFX cube uploadfailed face%u mip%u\n", face, mip);
        return false;
      }
    }
  td = {128,         128, 1, 1, KGFX_TEXTURE_TARGET | KGFX_TEXTURE_SAMPLED,
        KGFX_RGBA16F};
  if (kgfx_texture_create(d, &td, &hdr) != KGFX_OK)
    return false;
  vs = load("/app0/probe.vert.sb");
  ps = load("/app0/cube.frag.sb");
  pd = {};
  pd.vertex = vs;
  pd.fragment = ps;
  pd.color_format = KGFX_RGBA16F;
  pd.samples = 1;
  if (kgfx_pipeline_create(d, &pd, &opaque) != KGFX_OK)
    return false;
  free(const_cast<void *>(vs.data));
  free(const_cast<void *>(ps.data));
  kgfx_sampler_desc sd{};
  sd.min_filter = sd.mag_filter = KGFX_NEAREST;
  sd.mipmapped = 1;
  kgfx_sampler *sampler{};
  kgfx_sampler_create(d, &sd, &sampler);
  pass = {};
  pass.color = hdr;
  pass.width = pass.height = 128;
  pass.color_load = KGFX_CLEAR;
  binding = {};
  binding.textures[0] = cube;
  binding.samplers[0] = sampler;
  kgfx_frame_begin(d);
  kgfx_pass_begin(d, &pass);
  kgfx_bind_pipeline(d, opaque);
  kgfx_bind_resources(d, &binding);
  kgfx_draw(d, 0, 3, 1);
  kgfx_pass_end(d);
  kgfx_readback_begin(d, hdr, &read);
  done = kgfx_submit(d, 0);
  if (!done || kgfx_wait(d, done, 10000000000ull) != KGFX_OK)
    return false;
  if (kgfx_readback_map(d, read, &data) != KGFX_OK)
    return false;
  auto hp = reinterpret_cast<const uint16_t *>(
      static_cast<const unsigned char *>(data.pixels) + 64 * data.row_pitch +
      64 * 8);
  bool hdrok = hp[0] == 0 && hp[1] == 0x4200 && hp[2] == 0 && hp[3] == 0x3c00;
  printf("KGFX_GPU_CUBE_MIP_HDR_RESULT=%s half=%x,%x,%x,%x\n",
         hdrok ? "PASS" : "FAIL", hp[0], hp[1], hp[2], hp[3]);
  kgfx_readback_destroy(d, read);
  kgfx_pipeline_destroy(d, opaque);
  kgfx_sampler_destroy(d, sampler);
  kgfx_texture_destroy(d, cube);
  kgfx_texture_destroy(d, hdr);
  return depthok && sample_ok && blendok && hdrok;
}

static int presentation_check() {
  kgfx_gnm_config config{1, 1024 * 1024, 8 * 1024 * 1024};
  kgfx_device_desc desc{KGFX_ABI_VERSION, 320, 180, 3, &config, log_message,
                       nullptr};
  kgfx_device *device{};
  if (kgfx_device_create(&desc, &device) != KGFX_OK)
    return 1;
  bool ok = true;
  // Reuse each swapchain buffer multiple times. Reading the actual display
  // surface catches double sRGB encoding, independently of offscreen captures.
  constexpr unsigned frames = 600;
  for (unsigned frame = 0; frame != frames && ok; ++frame) {
    kgfx_pass_desc pass{};
    pass.color_load = KGFX_CLEAR;
    pass.clear_color[0] = .1f;
    pass.clear_color[1] = .25f;
    pass.clear_color[2] = .5f;
    pass.clear_color[3] = 1;
    ok = kgfx_frame_begin(device) == KGFX_OK &&
         kgfx_pass_begin(device, &pass) == KGFX_OK;
    kgfx_pass_end(device);
    kgfx_readback *read{};
    const bool capture = frame >= frames - 3;
    if (capture)
      ok &= kgfx_readback_begin(device, nullptr, &read) == KGFX_OK;
    const auto serial = kgfx_submit(device, 1);
    ok &= serial != 0;
    if (!capture)
      continue;
    ok &= serial && kgfx_wait(device, serial, 10'000'000'000ull) == KGFX_OK;
    kgfx_readback_data data{};
    ok &= kgfx_readback_map(device, read, &data) == KGFX_OK;
    if (ok) {
      auto *p = static_cast<const unsigned char *>(data.pixels);
      ok &= data.width == 320 && data.height == 180 &&
            data.format == KGFX_RGBA8;
      for (unsigned y = 0; y != data.height; ++y)
        for (unsigned x = 0; x != data.width; ++x) {
          auto *pixel = p + y * data.row_pitch + x * 4;
          ok &= abs(int(pixel[0]) - 26) <= 1 &&
                abs(int(pixel[1]) - 64) <= 1 &&
                abs(int(pixel[2]) - 128) <= 1 && pixel[3] == 255;
        }
    }
    kgfx_readback_destroy(device, read);
  }
  kgfx_device_destroy(device);
  printf("KGFX_GPU_PRESENTATION_RESULT=%s\n", ok ? "PASS" : "FAIL");
  return ok ? 0 : 1;
}

int main(int argc, char **argv) {
  if (argc == 2 && !strcmp(argv[1], "--present"))
    return presentation_check();
  const bool discard_only = argc == 2 && !strcmp(argv[1], "--discard-only");
  const bool buffer_overlap_only =
      argc == 2 && !strcmp(argv[1], "--buffer-overlap-only");
  if (argc != 1 && !discard_only && !buffer_overlap_only) {
    fprintf(stderr, "Usage: kgfx_gnm_probe [--present | --discard-only | "
                    "--buffer-overlap-only]\n");
    return 2;
  }
  kgfx_gnm_config backend{0, 1024 * 1024, 8 * 1024 * 1024};
  kgfx_device_desc desc{KGFX_ABI_VERSION, 128,         128,    3,
                        &backend,         log_message, nullptr};
  kgfx_device *d{};
  auto r = kgfx_device_create(&desc, &d);
  if (r) {
    printf("KGFX probe create failed %d\n", r);
    return 1;
  }
  if (discard_only) {
    // Isolate native discard/depth semantics from unrelated conformance cases.
    const bool ok = discard_depth_check(d);
    kgfx_device_destroy(d);
    return ok ? 0 : 6;
  }
  const bool overlap_ok = buffer_overlap_check(d);
  if (buffer_overlap_only) {
    kgfx_device_destroy(d);
    return overlap_ok ? 0 : 6;
  }
  kgfx_texture_desc td{
      128, 128, 1, 1, KGFX_TEXTURE_TARGET | KGFX_TEXTURE_SAMPLED, KGFX_RGBA8};
  kgfx_texture *t{};
  if (kgfx_texture_create(d, &td, &t) != KGFX_OK)
    return 2;
  kgfx_frame_begin(d);
  kgfx_pass_desc pass{};
  pass.color = t;
  pass.width = 128;
  pass.height = 128;
  pass.color_load = KGFX_CLEAR;
  pass.clear_color[0] = .25f;
  pass.clear_color[1] = .5f;
  pass.clear_color[2] = .75f;
  pass.clear_color[3] = 1;
  kgfx_pass_begin(d, &pass);
  kgfx_pass_end(d);
  kgfx_readback *read{};
  if (kgfx_readback_begin(d, t, &read) != KGFX_OK)
    return 3;
  auto done = kgfx_submit(d, 0);
  if (!done || kgfx_wait(d, done, 10000000000ull) != KGFX_OK) {
    printf("KGFX GPU TIMEOUT\n");
    return 4;
  }
  kgfx_readback_data data{};
  if (kgfx_readback_map(d, read, &data) != KGFX_OK)
    return 5;
  auto *p = static_cast<const unsigned char *>(data.pixels);
  bool ok = overlap_ok;
  for (size_t i = 0; i < data.size; i += 4)
    if (abs(int(p[i]) - 64) > 1 || abs(int(p[i + 1]) - 128) > 1 ||
        abs(int(p[i + 2]) - 191) > 1 || p[i + 3] != 255)
      ok = false;
  FILE *f = fopen("/data/captures/gpu-clear.ppm", "wb");
  if (f) {
    fprintf(f, "P6\n128 128\n255\n");
    for (size_t i = 0; i < data.size; i += 4)
      fwrite(p + i, 1, 3, f);
    fclose(f);
  }
  printf("KGFX_GPU_CLEAR_RESULT=%s %u,%u,%u,%u\n", ok ? "PASS" : "FAIL", p[0],
         p[1], p[2], p[3]);
  kgfx_readback_destroy(d, read);
  auto vs = load("/app0/probe.vert.sb"), ps = load("/app0/probe.frag.sb");
  if (vs.data && ps.data) {
    kgfx_pipeline_desc pd{};
    pd.vertex = vs;
    pd.fragment = ps;
    pd.color_format = KGFX_RGBA8;
    pd.samples = 1;
    kgfx_pipeline *pipeline{};
    if (kgfx_pipeline_create(d, &pd, &pipeline) != KGFX_OK) {
      printf("KGFX pipeline fail %s\n", kgfx_device_error(d));
      return 7;
    }
    free(const_cast<void *>(vs.data));
    free(const_cast<void *>(ps.data));
    kgfx_frame_begin(d);
    kgfx_pass_begin(d, &pass);
    kgfx_bind_pipeline(d, pipeline);
    kgfx_bindings bindings{};
    kgfx_bind_resources(d, &bindings);
    kgfx_draw(d, 0, 3, 1);
    kgfx_pass_end(d);
    kgfx_readback_begin(d, t, &read);
    done = kgfx_submit(d, 0);
    if (!done || kgfx_wait(d, done, 10000000000ull) != KGFX_OK) {
      printf("KGFX triangle GPU timeout\n");
      return 8;
    }
    if (kgfx_readback_map(d, read, &data) != KGFX_OK)
      return 9;
    auto *pixel = static_cast<const unsigned char *>(data.pixels) +
                  64 * data.row_pitch + 64 * 4;
    bool triangle = pixel[0] > 240 && pixel[1] < 10 && pixel[2] < 10;
    printf("KGFX_GPU_TRIANGLE_RESULT=%s center=%u,%u,%u\n",
           triangle ? "PASS" : "FAIL", pixel[0], pixel[1], pixel[2]);
    ok &= triangle;
    f = fopen("/data/captures/gpu-triangle.ppm", "wb");
    if (f) {
      fprintf(f, "P6\n128 128\n255\n");
      auto *image = static_cast<const unsigned char *>(data.pixels);
      for (size_t i = 0; i < data.size; i += 4)
        fwrite(image + i, 1, 3, f);
      fclose(f);
    }
    kgfx_readback_destroy(d, read);
    kgfx_pipeline_destroy(d, pipeline);
  } else {
    printf("KGFX triangle shaders absent\n");
    ok = false;
  }
  {
    auto vert = load("/app0/probe.vert.sb"),
         frag = load("/app0/textured.frag.sb");
    kgfx_pipeline_desc pd{};
    pd.vertex = vert;
    pd.fragment = frag;
    pd.color_format = KGFX_RGBA8;
    pd.samples = 1;
    kgfx_pipeline *pipeline{};
    if (kgfx_pipeline_create(d, &pd, &pipeline) != KGFX_OK) {
      printf("KGFX texture pipeline failed %s\n", kgfx_device_error(d));
      return 10;
    }
    free(const_cast<void *>(vert.data));
    free(const_cast<void *>(frag.data));
    kgfx_texture_desc image_desc{2, 2, 1, 1, KGFX_TEXTURE_SAMPLED, KGFX_RGBA8};
    kgfx_texture *image{};
    if (kgfx_texture_create(d, &image_desc, &image) != KGFX_OK)
      return 11;
    unsigned char colors[16] = {128, 64, 32,  255, 0,   255, 0,   255,
                                0,   0,  255, 255, 255, 255, 255, 255};
    if (kgfx_texture_upload(d, image, 0, 0, colors, 8) != KGFX_OK) {
      printf("KGFX upload failed\n");
      return 12;
    }
    float tint[4] = {.5f, 1, 1, 1};
    kgfx_buffer_desc bd{sizeof(tint), KGFX_BUFFER_UNIFORM, tint};
    kgfx_buffer *uniform{};
    kgfx_buffer_create(d, &bd, &uniform);
    kgfx_sampler_desc sd{};
    sd.min_filter = KGFX_NEAREST;
    sd.mag_filter = KGFX_NEAREST;
    kgfx_sampler *sampler{};
    kgfx_sampler_create(d, &sd, &sampler);
    kgfx_frame_begin(d);
    kgfx_pass_begin(d, &pass);
    kgfx_bind_pipeline(d, pipeline);
    kgfx_bindings binding{};
    binding.uniforms[0] = uniform;
    binding.uniform_size[0] = sizeof(tint);
    binding.textures[0] = image;
    binding.samplers[0] = sampler;
    kgfx_bind_resources(d, &binding);
    kgfx_draw(d, 0, 3, 1);
    kgfx_pass_end(d);
    kgfx_readback_begin(d, t, &read);
    done = kgfx_submit(d, 0);
    if (!done || kgfx_wait(d, done, 10000000000ull) != KGFX_OK)
      return 13;
    if (kgfx_readback_map(d, read, &data) != KGFX_OK)
      return 14;
    auto *pixel = static_cast<const unsigned char *>(data.pixels) +
                  64 * data.row_pitch + 64 * 4;
    bool texture = pixel[0] >= 63 && pixel[0] <= 65 && pixel[1] >= 63 &&
                   pixel[1] <= 65 && pixel[2] >= 31 && pixel[2] <= 33;
    printf("KGFX_GPU_TEXTURE_RESULT=%s center=%u,%u,%u\n",
           texture ? "PASS" : "FAIL", pixel[0], pixel[1], pixel[2]);
    ok &= texture;
    kgfx_readback_destroy(d, read);
    kgfx_pipeline_destroy(d, pipeline);
    kgfx_sampler_destroy(d, sampler);
    kgfx_buffer_destroy(d, uniform);
    kgfx_texture_destroy(d, image);
  }
  {
    kgfx_texture *msaa{};
    auto msdesc = td;
    msdesc.samples = 4;
    if (kgfx_texture_create(d, &msdesc, &msaa) != KGFX_OK) {
      printf("KGFX MSAA create failed: %s\n", kgfx_device_error(d));
      return 15;
    }
    auto vert = load("/app0/probe.vert.sb"), frag = load("/app0/probe.frag.sb");
    kgfx_pipeline_desc pd{};
    pd.vertex = vert;
    pd.fragment = frag;
    pd.color_format = KGFX_RGBA8;
    pd.samples = 4;
    kgfx_pipeline *pipeline{};
    if (kgfx_pipeline_create(d, &pd, &pipeline) != KGFX_OK)
      return 16;
    free(const_cast<void *>(vert.data));
    free(const_cast<void *>(frag.data));
    kgfx_frame_begin(d);
    auto mspass = pass;
    mspass.color = msaa;
    mspass.clear_color[0] = mspass.clear_color[1] = mspass.clear_color[2] = 0;
    kgfx_pass_begin(d, &mspass);
    kgfx_bind_pipeline(d, pipeline);
    kgfx_bindings binding{};
    kgfx_bind_resources(d, &binding);
    kgfx_draw(d, 0, 3, 1);
    kgfx_pass_end(d);
    if (kgfx_texture_resolve(d, msaa, t) != KGFX_OK) {
      printf("KGFX resolve failed: %s\n", kgfx_device_error(d));
      return 17;
    }
    kgfx_readback_begin(d, t, &read);
    done = kgfx_submit(d, 0);
    if (!done || kgfx_wait(d, done, 10000000000ull) != KGFX_OK)
      return 18;
    if (kgfx_readback_map(d, read, &data) != KGFX_OK)
      return 19;
    auto *pixels = static_cast<const unsigned char *>(data.pixels);
    unsigned partial = 0;
    for (unsigned y = 0; y < 128; ++y)
      for (unsigned x = 0; x < 128; ++x) {
        unsigned r = pixels[y * data.row_pitch + x * 4];
        if (r > 10 && r < 240)
          ++partial;
      }
    auto *center = pixels + 64 * data.row_pitch + 64 * 4;
    bool msok =
        center[0] > 240 && center[1] < 10 && center[2] < 10 && partial > 10;
    printf("KGFX_GPU_MSAA_RESULT=%s center=%u,%u,%u partial=%u\n",
           msok ? "PASS" : "FAIL", center[0], center[1], center[2], partial);
    ok &= msok;
    f = fopen("/data/captures/gpu-msaa.ppm", "wb");
    if (f) {
      fprintf(f, "P6\n128 128\n255\n");
      for (size_t i = 0; i < data.size; i += 4)
        fwrite(pixels + i, 1, 3, f);
      fclose(f);
    }
    kgfx_readback_destroy(d, read);
    kgfx_pipeline_destroy(d, pipeline);
    kgfx_texture_destroy(d, msaa);
  }
  ok &= culling_check(d, t);
  ok &= vertex_abi_checks(d, t);
  ok &= precise_color_checks(d);
  ok &= transfer_checks(d);
  ok &= attachment_storage_checks(d, t);
  ok &= hdr_msaa_check(d);
  ok &= hdr_blend_checks(d);
  ok &= feature_checks(d, t);
  ok &= discard_depth_check(d);
  kgfx_texture_destroy(d, t);
  kgfx_device_destroy(d);
  return ok ? 0 : 6;
}
