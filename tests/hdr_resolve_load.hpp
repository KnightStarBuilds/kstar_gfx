// SPDX-License-Identifier: MIT
#pragma once
#include <kstar_gfx/gfx.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>

// Shared native/guest expectations. The supplied attributes shader scales x/y
// by (.4,.8), offsets instance-zero x by -.45, and uses uniform control.x depth.
// Shader artifacts remain owned by the caller.
static inline bool hdr_resolve_load_check(kgfx_device *d,
                                          kgfx_shader_artifact vs,
                                          kgfx_shader_artifact ps) {
  // A peeled transparency frame repeatedly resolves opaque HDR, changes to
  // single-sample targets, then resumes the same multisample color/depth pair.
  // Keep every snapshot so later LOAD passes cannot conceal an earlier error.
  const float vertices[] = {
      -1.375f, -1.25f, 0, 1, 1, 1, 1,
       8.625f, -1.25f, 0, 1, 1, 1, 1,
      -1.375f,  3.75f, 0, 1, 1, 1, 1};
  const float triangle[] = {
      -.8f, -.8f, 0, 1, 1, 1, 1,
       .8f, -.8f, 0, 1, 1, 1, 1,
          0, .8f, 0, 1, 1, 1, 1};
  const float parameters[][8] = {
      {2, 4, 8, 1, .7f, 0, 0, 0},
      {.5f, .25f, 1, .5f, .2f, 0, 0, 0},
      {16, 0, 0, .5f, .8f, 0, 0, 0},
      {1, 2, 4, 1, .1f, 0, 0, 0}};
  kgfx_buffer *vertex{}, *occluder{}, *uniforms[4]{};
  kgfx_buffer_desc bd{sizeof(vertices), KGFX_BUFFER_VERTEX, vertices};
  if (kgfx_buffer_create(d, &bd, &vertex) != KGFX_OK)
    return false;
  bd = {sizeof(triangle), KGFX_BUFFER_VERTEX, triangle};
  if (kgfx_buffer_create(d, &bd, &occluder) != KGFX_OK)
    return false;
  for (unsigned i = 0; i < 4; ++i) {
    bd = {sizeof(parameters[i]), KGFX_BUFFER_UNIFORM, parameters[i]};
    if (kgfx_buffer_create(d, &bd, &uniforms[i]) != KGFX_OK)
      return false;
  }
  kgfx_texture_desc td{
      128, 128, 1, 4, KGFX_TEXTURE_TARGET | KGFX_TEXTURE_SAMPLED, KGFX_RGBA16F};
  kgfx_texture *color{}, *depth{}, *snapshots[4]{}, *single{}, *single_depth{};
  if (kgfx_texture_create(d, &td, &color) != KGFX_OK)
    return false;
  td.format = KGFX_DEPTH32F;
  if (kgfx_texture_create(d, &td, &depth) != KGFX_OK)
    return false;
  td.samples = 1;
  td.format = KGFX_RGBA16F;
  for (auto &snapshot : snapshots)
    if (kgfx_texture_create(d, &td, &snapshot) != KGFX_OK)
      return false;
  td.width = td.height = 16;
  if (kgfx_texture_create(d, &td, &single) != KGFX_OK)
    return false;
  td.format = KGFX_DEPTH32F;
  if (kgfx_texture_create(d, &td, &single_depth) != KGFX_OK)
    return false;

  kgfx_pipeline_desc pd{};
  pd.vertex = vs;
  pd.fragment = ps;
  pd.color_format = KGFX_RGBA16F;
  pd.depth_format = KGFX_DEPTH32F;
  pd.samples = 4;
  pd.depth_test = pd.depth_write = 1;
  pd.depth_compare = KGFX_COMPARE_LESS;
  pd.vertex_stride[0] = 7 * sizeof(float);
  pd.attribute_count = 2;
  pd.attributes[0] = {0, 0, 0, 0, KGFX_FLOAT3};
  pd.attributes[1] = {3, 3 * sizeof(float), 0, 0, KGFX_FLOAT4};
  kgfx_pipeline *opaque{}, *transparent{};
  if (kgfx_pipeline_create(d, &pd, &opaque) != KGFX_OK)
    return false;
  pd.depth_write = 0;
  pd.blend = KGFX_BLEND_PREMULTIPLIED;
  if (kgfx_pipeline_create(d, &pd, &transparent) != KGFX_OK)
    return false;

  if (kgfx_frame_begin(d) != KGFX_OK)
    return false;
  kgfx_pass_desc pass{};
  pass.color = color;
  pass.depth = depth;
  pass.width = pass.height = 128;
  pass.color_load = pass.depth_load = KGFX_CLEAR;
  pass.clear_depth = 1;
  pass.clear_color[3] = 1;
  if (kgfx_pass_begin(d, &pass) != KGFX_OK)
    return false;
  kgfx_viewport(d, 0, 0, 128, 128);
  kgfx_scissor(d, 0, 0, 128, 128);
  kgfx_bind_pipeline(d, opaque);
  kgfx_bindings bindings{};
  bindings.vertex[0] = vertex;
  bindings.uniforms[0] = uniforms[0];
  bindings.uniform_size[0] = sizeof(parameters[0]);
  kgfx_bind_resources(d, &bindings);
  kgfx_draw(d, 0, 3, 1);
  bindings.vertex[0] = occluder;
  bindings.uniforms[0] = uniforms[3];
  kgfx_bind_resources(d, &bindings);
  kgfx_draw(d, 0, 3, 1);
  bindings.vertex[0] = vertex;
  kgfx_pass_end(d);

  kgfx_readback *reads[4]{};
  for (unsigned phase = 0; phase < 4; ++phase) {
    if (phase) {
      kgfx_pass_desc small{};
      small.color = single;
      small.depth = single_depth;
      small.width = small.height = 16;
      small.color_load = small.depth_load = KGFX_CLEAR;
      small.clear_depth = .05f;
      small.clear_color[1] = .125f;
      if (kgfx_pass_begin(d, &small) != KGFX_OK)
        return false;
      kgfx_pass_end(d);
      pass.color_load = pass.depth_load = KGFX_LOAD;
      if (kgfx_texture_transition(d, color, KGFX_ACCESS_ATTACHMENT) != KGFX_OK ||
          kgfx_pass_begin(d, &pass) != KGFX_OK)
        return false;
      kgfx_viewport(d, 0, 0, 128, 128);
      kgfx_scissor(d, 16, 16, 96, 96);
      kgfx_bind_pipeline(d, transparent);
      bindings.uniforms[0] = uniforms[1];
      kgfx_bind_resources(d, &bindings);
      kgfx_draw(d, 0, 3, 1);
      // Depth .8 must remain behind the opaque .7, including outside the
      // overlay. A lost/restored-wrong depth attachment turns these pixels red.
      kgfx_scissor(d, 0, 0, 128, 128);
      bindings.uniforms[0] = uniforms[2];
      kgfx_bind_resources(d, &bindings);
      kgfx_draw(d, 0, 3, 1);
      kgfx_pass_end(d);
    }
    if (kgfx_texture_resolve(d, color, snapshots[phase]) != KGFX_OK ||
        kgfx_readback_begin(d, snapshots[phase], &reads[phase]) != KGFX_OK)
      return false;
  }
  const auto done = kgfx_submit(d, 0);
  if (!done || kgfx_wait(d, done, 10000000000ull) != KGFX_OK)
    return false;

  // All values and quarter-coverage mixtures are exactly representable in
  // binary16. Each occluder sample stays (1,2,4), while uncovered samples follow
  // C[n+1] = (.5,.25,1) + .5*C[n]. Infer its coverage from the first snapshot so
  // this checks preserved per-sample depth without assuming a raster edge rule.
  const float expected[4][4] = {
      {2, 4, 8, 1}, {1.5f, 2.25f, 5, 1},
      {1.25f, 1.375f, 3.5f, 1}, {1.125f, .9375f, 2.75f, 1}};
  auto half_value = [](uint16_t h) {
    const unsigned exponent = (h >> 10) & 31;
    if (exponent == 31)
      return NAN;
    const float value = exponent
        ? ldexpf(1 + float(h & 1023) / 1024, int(exponent) - 15)
        : ldexpf(float(h & 1023), -24);
    return h & 0x8000 ? -value : value;
  };
  uint8_t coverage[128 * 128]{};
  unsigned partial = 0;
  bool ok = true;
  for (unsigned phase = 0; phase < 4; ++phase) {
    kgfx_readback_data data{};
    if (kgfx_readback_map(d, reads[phase], &data) != KGFX_OK)
      return false;
    unsigned errors = 0;
    for (unsigned y = 2; y < 126; ++y)
      for (unsigned x = 2; x < 126; ++x) {
        const auto *pixel = reinterpret_cast<const uint16_t *>(
            static_cast<const unsigned char *>(data.pixels) + y * data.row_pitch + x * 8);
        if (!phase) {
          const float amount = (2 - half_value(pixel[0])) * 4;
          const bool valid = isfinite(amount) && amount >= 0 && amount <= 4 &&
                             amount == floorf(amount);
          errors += !valid;
          coverage[y * 128 + x] = valid ? uint8_t(amount) : 0;
          partial += valid && amount > 0 && amount < 4;
        }
        const float amount = float(coverage[y * 128 + x]) * .25f;
        const auto *base = expected[x >= 16 && x < 112 && y >= 16 && y < 112 ? phase : 0];
        bool correct = true;
        for (unsigned channel = 0; channel < 4; ++channel) {
          const float wanted = base[channel] * (1 - amount) +
                               parameters[3][channel] * amount;
          correct &= half_value(pixel[channel]) == wanted;
        }
        errors += !correct;
      }
    const auto *center = reinterpret_cast<const uint16_t *>(
        static_cast<const unsigned char *>(data.pixels) + 64 * data.row_pitch + 64 * 8);
    printf("KGFX HDR resolve/LOAD phase=%u errors=%u centerhalf=%x,%x,%x,%x\n",
           phase, errors, center[0], center[1], center[2], center[3]);
    ok &= errors == 0;
    kgfx_readback_destroy(d, reads[phase]);
    kgfx_texture_destroy(d, snapshots[phase]);
  }
  ok &= partial > 10;
  printf("KGFX_GPU_HDR_RESOLVE_LOAD_4X_RESULT=%s partial=%u\n", ok ? "PASS" : "FAIL", partial);
  kgfx_pipeline_destroy(d, opaque);
  kgfx_pipeline_destroy(d, transparent);
  kgfx_texture_destroy(d, color);
  kgfx_texture_destroy(d, depth);
  kgfx_texture_destroy(d, single);
  kgfx_texture_destroy(d, single_depth);
  kgfx_buffer_destroy(d, vertex);
  kgfx_buffer_destroy(d, occluder);
  for (auto *uniform : uniforms)
    kgfx_buffer_destroy(d, uniform);
  return ok;
}
