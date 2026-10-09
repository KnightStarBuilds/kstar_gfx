/* SPDX-License-Identifier: MIT
 * kstar_gfx offline asset records. Little endian IEEE-754; not a runtime API.
 * Rows follow glTF UV coordinates (PNG top row is v=0). Cube faces use OpenGL
 * +X,-X,+Y,-Y,+Z,-Z with the first stored row at t=0. All offsets are bytes. */
#ifndef KGFX_ASSET_FORMAT_H
#define KGFX_ASSET_FORMAT_H
#include <stdint.h>
#define KGFX_ASSET_VERSION 2u
#define KGFX_ASSET_TEXTURE_PATH 160u
#define KGFX_ASSET_MAX_MIPS 16u
#define KGFX_ENVIRONMENT_MIN_SPECULAR_SIZE 16u
#define KGFX_ASSET_RGBA8 1u
#define KGFX_ASSET_RGBA32F 2u
#define KGFX_ASSET_ALPHA_OPAQUE 0u
#define KGFX_ASSET_ALPHA_MASK 1u
#define KGFX_ASSET_ALPHA_BLEND 2u
/* File layout: header, materials, primitives, vertices, uint32 indices. */
typedef struct kgfx_model_file {
  char magic[8]; /* "KGMODEL\0" */
  uint32_t version, material_count, primitive_count, vertex_count, index_count;
  uint32_t reserved;
  uint64_t materials_offset, primitives_offset, vertices_offset, indices_offset;
  float bounds_min[3], bounds_max[3];
} kgfx_model_file;
typedef struct kgfx_asset_vertex {
  float position[3], normal[3], tangent[4], uv[2];
} kgfx_asset_vertex;
typedef struct kgfx_asset_primitive {
  uint32_t first_index, index_count, material_index, reserved;
} kgfx_asset_primitive;
typedef struct kgfx_asset_material {
  float base_color[4], emissive[3], metallic, roughness, normal_scale,
      occlusion_strength, alpha_cutoff;
  uint32_t alpha_mode, double_sided, reserved[2];
  char base_color_texture[KGFX_ASSET_TEXTURE_PATH];
  char normal_texture[KGFX_ASSET_TEXTURE_PATH];
  char material_texture[KGFX_ASSET_TEXTURE_PATH]; /* R=AO G=roughness B=metallic
                                                   */
  char emissive_texture[KGFX_ASSET_TEXTURE_PATH];
} kgfx_asset_material;
/* Texture payload has mip0..N sequentially, no padding, RGBA8 or RGBA32F.
 * Color maps store sRGB bytes; the mip chain is filtered in linear light.
 * Normal maps are normalized after filtering. Data maps filter linearly. */
typedef struct kgfx_texture_file {
  char magic[8]; /* "KGTEX\0\0\0" */
  uint32_t version, width, height, mip_count, format, srgb;
  uint64_t data_offset, data_bytes;
} kgfx_texture_file;
/* Specular cube is mip-major, then face-major, then row-major RGBA32F.
 * mip0 is also the sky. Version 2 stops at 16x16 per face to preserve angular
 * detail at full roughness; roughness = mip / (mip_count - 1). This is an
 * explicitly bounded reflection mip chain, not a complete chain down to 1x1.
 * Irradiance stores physical integral/pi: the PBR
 * shader multiplies it directly by baseColor*(1-metallic). BRDF stores
 * split-sum scale,bias in RG, BA=0,1. */
typedef struct kgfx_environment_file {
  char magic[8]; /* "KGENV\0\0\0" */
  uint32_t version, cube_size, mip_count, irradiance_size, brdf_size, reserved;
  uint64_t specular_offset, irradiance_offset, brdf_offset, file_bytes;
} kgfx_environment_file;
#endif
