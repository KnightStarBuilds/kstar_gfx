// SPDX-License-Identifier: MIT
// Offline conversions only: renderer binaries have no image/glTF/EXR loader.
#include "kgfx_asset_format.h"
#define CGLTF_IMPLEMENTATION
#include <cgltf.h>
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#include <stb_image.h>
#define TINYEXR_IMPLEMENTATION
#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <map>
#include <mikktspace.h>
#include <numbers>
#include <span>
#include <stdexcept>
#include <string>
#include <thread>
#include <tinyexr.h>
#include <vector>
namespace fs = std::filesystem;
constexpr float pi = std::numbers::pi_v<float>;
struct V3 {
  float x = 0, y = 0, z = 0;
  V3 operator+(V3 b) const { return {x + b.x, y + b.y, z + b.z}; }
  V3 operator-(V3 b) const { return {x - b.x, y - b.y, z - b.z}; }
  V3 operator*(float b) const { return {x * b, y * b, z * b}; }
};
float dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
V3 cross(V3 a, V3 b) {
  return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
V3 normalize(V3 a) {
  float l = std::sqrt(dot(a, a));
  return l > 1e-15f ? a * (1 / l) : V3{0, 1, 0};
}
V3 load3(const float *a) { return {a[0], a[1], a[2]}; }
void store3(float *a, V3 b) {
  a[0] = b.x;
  a[1] = b.y;
  a[2] = b.z;
}
float linear(float x) {
  return x <= .04045f ? x / 12.92f : std::pow((x + .055f) / 1.055f, 2.4f);
}
float srgb(float x) {
  return x <= .0031308f ? 12.92f * x : 1.055f * std::pow(x, 1 / 2.4f) - .055f;
}
float radical(uint32_t bits) {
  bits = (bits << 16u) | (bits >> 16u);
  bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
  bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
  bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
  bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
  return float(bits) * 2.3283064365386963e-10f;
}
V3 tangent_to_world(V3 v, V3 n) {
  V3 t = normalize(cross(std::abs(n.z) < .999f ? V3{0, 0, 1} : V3{1, 0, 0}, n));
  return t * v.x + cross(n, t) * v.y + n * v.z;
}
V3 ggx(float u, float v, float roughness) {
  float a = roughness * roughness, phi = 2 * pi * u;
  float c = std::sqrt((1 - v) / (1 + (a * a - 1) * v));
  float s = std::sqrt(std::max(0.f, 1 - c * c));
  return {std::cos(phi) * s, std::sin(phi) * s, c};
}
V3 cube_direction(int face, float u, float v) {
  float a = 2 * u - 1, b = 2 * v - 1;
  switch (face) {
  case 0:
    return normalize({1, -b, -a});
  case 1:
    return normalize({-1, -b, a});
  case 2:
    return normalize({a, 1, b});
  case 3:
    return normalize({a, -1, -b});
  case 4:
    return normalize({a, -b, 1});
  default:
    return normalize({-a, -b, -1});
  }
}
void parallel_rows(size_t count, const auto &fn) {
  std::atomic<size_t> next{0};
  std::vector<std::thread> workers;
  unsigned n = std::min(12u, std::max(1u, std::thread::hardware_concurrency()));
  for (unsigned i = 0; i < n; ++i)
    workers.emplace_back([&] {
      for (size_t y; (y = next.fetch_add(1)) < count;)
        fn(y);
    });
  for (auto &w : workers)
    w.join();
}
template <class T> void write(std::ofstream &out, const T &v) {
  out.write(reinterpret_cast<const char *>(&v), sizeof(v));
}
template <class T> void write_vec(std::ofstream &out, const std::vector<T> &v) {
  out.write(reinterpret_cast<const char *>(v.data()),
            std::streamsize(v.size() * sizeof(T)));
}
struct Writer {
  fs::path final, part;
  std::ofstream out;
  explicit Writer(fs::path p) : final(p), part(p.string() + ".part") {
    fs::create_directories(p.parent_path());
    out.open(part, std::ios::binary);
    if (!out)
      throw std::runtime_error("Cannot write " + p.string());
  }
  void finish() {
    out.close();
    if (!out)
      throw std::runtime_error("Failed writing " + part.string());
    if (fs::exists(final))
      fs::remove(final);
    fs::rename(part, final);
  }
};
struct Image {
  int w = 0, h = 0;
  std::vector<float> rgba;
};
Image read_png(const fs::path &file) {
  int w, h, c;
  stbi_us *p = stbi_load_16(file.string().c_str(), &w, &h, &c, 4);
  if (!p)
    throw std::runtime_error("PNG " + file.string() + ": " +
                             stbi_failure_reason());
  Image im{w, h, std::vector<float>(size_t(w) * h * 4)};
  for (size_t i = 0; i < im.rgba.size(); ++i)
    im.rgba[i] = p[i] / 65535.f;
  stbi_image_free(p);
  return im;
}
Image downsample(const Image &source, bool color, bool normal) {
  Image result{std::max(1, source.w / 2), std::max(1, source.h / 2), {}};
  result.rgba.resize(size_t(result.w) * result.h * 4);
  for (int y = 0; y < result.h; ++y)
    for (int x = 0; x < result.w; ++x) {
      float sum[4]{};
      for (int j = 0; j < 2; ++j)
        for (int i = 0; i < 2; ++i) {
          size_t p = (size_t(std::min(source.h - 1, y * 2 + j)) * source.w +
                      std::min(source.w - 1, x * 2 + i)) *
                     4;
          for (int c = 0; c < 4; ++c)
            sum[c] += color && c < 3
                          ? linear(source.rgba[p + c]) * source.rgba[p + 3]
                          : source.rgba[p + c];
        }
      size_t p = (size_t(y) * result.w + x) * 4;
      for (int c = 0; c < 4; ++c)
        result.rgba[p + c] = color && c < 3
                                 ? srgb(sum[c] / std::max(sum[3], 1e-10f))
                                 : sum[c] * .25f;
      if (normal) {
        V3 n = normalize({result.rgba[p] * 2 - 1, result.rgba[p + 1] * 2 - 1,
                          result.rgba[p + 2] * 2 - 1});
        result.rgba[p] = n.x * .5f + .5f;
        result.rgba[p + 1] = n.y * .5f + .5f;
        result.rgba[p + 2] = n.z * .5f + .5f;
      }
    }
  return result;
}
// Equirectangular rows cover unequal solid angles. The last mip must be
// the spherical mean, not a mean that overweights the poles.
Image downsample_environment(const Image &source) {
  Image out{std::max(1, source.w / 2), std::max(1, source.h / 2), {}};
  out.rgba.resize(size_t(out.w) * out.h * 4);
  for (int y = 0; y < out.h; ++y)
    for (int x = 0; x < out.w; ++x) {
      double sum[4]{}, weight = 0;
      for (int j = 0; j < 2; ++j)
        for (int i = 0; i < 2; ++i) {
          int sy = std::min(source.h - 1, 2 * y + j),
              sx = std::min(source.w - 1, 2 * x + i);
          double solid_angle = std::cos(std::numbers::pi * sy / source.h) -
                               std::cos(std::numbers::pi * (sy + 1) / source.h);
          size_t offset = (size_t(sy) * source.w + sx) * 4;
          for (int c = 0; c < 4; ++c)
            sum[c] += source.rgba[offset + c] * solid_angle;
          weight += solid_angle;
        }
      size_t offset = (size_t(y) * out.w + x) * 4;
      for (int c = 0; c < 4; ++c)
        out.rgba[offset + c] = float(sum[c] / weight);
    }
  return out;
}
void cook_texture(const fs::path &source, const fs::path &output, bool color,
                  bool normal, const fs::path &alpha = {}) {
  Image im = read_png(source);
  if (im.w != 2048 || im.h != 2048)
    throw std::runtime_error("Expected curated texture at 2048x2048: " +
                             source.string());
  if (!alpha.empty()) {
    Image a = read_png(alpha);
    if (a.w != im.w || a.h != im.h)
      throw std::runtime_error("Alpha size mismatch");
    for (size_t p = 0; p < im.rgba.size(); p += 4)
      im.rgba[p + 3] = a.rgba[p];
  }
  kgfx_texture_file h{};
  std::memcpy(h.magic, "KGTEX", 5);
  h.version = KGFX_ASSET_VERSION;
  h.width = im.w;
  h.height = im.h;
  h.format = KGFX_ASSET_RGBA8;
  h.srgb = color;
  h.data_offset = sizeof(h);
  std::vector<uint8_t> payload;
  for (;;) {
    ++h.mip_count;
    for (float value : im.rgba)
      payload.push_back(
          uint8_t(std::lround(std::clamp(value, 0.f, 1.f) * 255)));
    if (im.w == 1 && im.h == 1)
      break;
    im = downsample(im, color, normal);
  }
  h.data_bytes = payload.size();
  Writer out(output);
  write(out.out, h);
  write_vec(out.out, payload);
  out.finish();
}
std::string texture_path(const cgltf_texture_view &view,
                         const fs::path &model_dir, const fs::path &out_dir,
                         bool color, bool normal, bool glass,
                         std::map<std::string, std::string> &done) {
  if (!view.texture)
    return {};
  if (view.texcoord != 0 || view.has_transform)
    throw std::runtime_error("Cooker requires UV0 without texture transforms");
  const auto *image = view.texture->image;
  if (!image || !image->uri)
    throw std::runtime_error("Expected external curated PNG image");
  fs::path source = model_dir / image->uri;
  source.replace_extension(".png");
  fs::path alpha;
  if (glass) {
    alpha = source;
    std::string s = alpha.filename().string();
    auto at = s.find("_diff_");
    if (at == std::string::npos)
      throw std::runtime_error("Glass diffuse naming mismatch");
    s.replace(at, 6, "_alpha_");
    alpha = alpha.parent_path() / s;
  }
  std::string stem = source.stem().string() + (glass ? "_glass" : "");
  std::string relative = "textures/" + stem + ".kgtex";
  std::string key = source.string() + (glass ? ":glass" : "") +
                    (color    ? ":srgb"
                     : normal ? ":normal"
                              : ":data");
  if (auto it = done.find(key); it != done.end())
    return it->second;
  cook_texture(source, out_dir / relative, color, normal, alpha);
  done[key] = relative;
  return relative;
}
void put_path(char *out, const std::string &s) {
  if (s.size() >= KGFX_ASSET_TEXTURE_PATH)
    throw std::runtime_error("Texture path too long");
  std::memcpy(out, s.c_str(), s.size() + 1);
}
struct TangentInput {
  std::vector<kgfx_asset_vertex> *vertices;
};
int mikk_faces(const SMikkTSpaceContext *c) {
  return int(static_cast<TangentInput *>(c->m_pUserData)->vertices->size() / 3);
}
int mikk_verts(const SMikkTSpaceContext *, int) { return 3; }
void mikk_pos(const SMikkTSpaceContext *c, float p[], int f, int v) {
  std::memcpy(p,
              (*static_cast<TangentInput *>(c->m_pUserData)
                    ->vertices)[size_t(f) * 3 + v]
                  .position,
              12);
}
void mikk_normal(const SMikkTSpaceContext *c, float p[], int f, int v) {
  std::memcpy(p,
              (*static_cast<TangentInput *>(c->m_pUserData)
                    ->vertices)[size_t(f) * 3 + v]
                  .normal,
              12);
}
void mikk_uv(const SMikkTSpaceContext *c, float p[], int f, int v) {
  std::memcpy(p,
              (*static_cast<TangentInput *>(c->m_pUserData)
                    ->vertices)[size_t(f) * 3 + v]
                  .uv,
              8);
}
void mikk_set(const SMikkTSpaceContext *c, const float t[], float sign, int f,
              int v) {
  auto &dst = (*static_cast<TangentInput *>(c->m_pUserData)
                    ->vertices)[size_t(f) * 3 + v];
  std::memcpy(dst.tangent, t, 12);
  dst.tangent[3] = sign;
}
void cook_model(const fs::path &source, const fs::path &output) {
  cgltf_options options{};
  cgltf_data *raw = nullptr;
  if (cgltf_parse_file(&options, source.string().c_str(), &raw) !=
      cgltf_result_success)
    throw std::runtime_error("Invalid glTF: " + source.string());
  std::unique_ptr<cgltf_data, decltype(&cgltf_free)> data(raw, cgltf_free);
  if (cgltf_load_buffers(&options, raw, source.string().c_str()) !=
          cgltf_result_success ||
      cgltf_validate(raw) != cgltf_result_success)
    throw std::runtime_error("Invalid glTF buffers");
  std::vector<kgfx_asset_material> materials;
  std::map<std::string, std::string> done;
  for (size_t i = 0; i < raw->materials_count; ++i) {
    const auto &s = raw->materials[i];
    if (!s.has_pbr_metallic_roughness)
      throw std::runtime_error("Expected PBR metallic/roughness material");
    const auto &p = s.pbr_metallic_roughness;
    kgfx_asset_material d{};
    std::memcpy(d.base_color, p.base_color_factor, 16);
    std::memcpy(d.emissive, s.emissive_factor, 12);
    d.metallic = p.metallic_factor;
    d.roughness = p.roughness_factor;
    d.normal_scale = s.normal_texture.scale;
    d.occlusion_strength =
        s.occlusion_texture.texture ? s.occlusion_texture.scale : 1;
    d.alpha_cutoff = s.alpha_cutoff;
    d.alpha_mode = s.alpha_mode;
    d.double_sided = s.double_sided;
    bool glass =
        s.alpha_mode == cgltf_alpha_mode_blend &&
        std::string(s.name ? s.name : "").find("glass") != std::string::npos;
    put_path(d.base_color_texture,
             texture_path(p.base_color_texture, source.parent_path(),
                          output.parent_path(), true, false, glass, done));
    put_path(d.normal_texture,
             texture_path(s.normal_texture, source.parent_path(),
                          output.parent_path(), false, true, false, done));
    put_path(d.material_texture,
             texture_path(p.metallic_roughness_texture, source.parent_path(),
                          output.parent_path(), false, false, false, done));
    put_path(d.emissive_texture,
             texture_path(s.emissive_texture, source.parent_path(),
                          output.parent_path(), true, false, false, done));
    materials.push_back(d);
  }
  std::vector<kgfx_asset_vertex> vertices;
  std::vector<uint32_t> indices;
  std::vector<kgfx_asset_primitive> primitives;
  for (size_t n = 0; n < raw->nodes_count; ++n) {
    auto &node = raw->nodes[n];
    if (!node.mesh)
      continue;
    float m[16];
    cgltf_node_transform_world(&node, m);
    V3 col0{m[0], m[1], m[2]}, col1{m[4], m[5], m[6]}, col2{m[8], m[9], m[10]};
    float determinant = dot(col0, cross(col1, col2));
    if (std::abs(determinant) < 1e-10f)
      throw std::runtime_error("Singular model transform");
    V3 inv0 = cross(col1, col2) * (1 / determinant),
       inv1 = cross(col2, col0) * (1 / determinant),
       inv2 = cross(col0, col1) * (1 / determinant);
    for (size_t p = 0; p < node.mesh->primitives_count; ++p) {
      auto &primitive = node.mesh->primitives[p];
      if (primitive.type != cgltf_primitive_type_triangles)
        throw std::runtime_error("Only triangle primitives supported");
      const cgltf_accessor *pos = nullptr, *normal = nullptr, *uv = nullptr;
      for (size_t a = 0; a < primitive.attributes_count; ++a) {
        auto &attr = primitive.attributes[a];
        if (attr.type == cgltf_attribute_type_position)
          pos = attr.data;
        if (attr.type == cgltf_attribute_type_normal)
          normal = attr.data;
        if (attr.type == cgltf_attribute_type_texcoord && attr.index == 0)
          uv = attr.data;
      }
      if (!pos || !normal || !uv)
        throw std::runtime_error("Position, normal and UV0 required");
      size_t count = primitive.indices ? primitive.indices->count : pos->count;
      if (count % 3)
        throw std::runtime_error("Invalid triangle indices");
      std::vector<kgfx_asset_vertex> expanded(count);
      for (size_t i = 0; i < count; ++i) {
        size_t idx = primitive.indices
                         ? cgltf_accessor_read_index(primitive.indices, i)
                         : i;
        auto &v = expanded[i];
        float p3[3], n3[3];
        if (!cgltf_accessor_read_float(pos, idx, p3, 3) ||
            !cgltf_accessor_read_float(normal, idx, n3, 3) ||
            !cgltf_accessor_read_float(uv, idx, v.uv, 2))
          throw std::runtime_error("Invalid accessor");
        V3 pt = col0 * p3[0] + col1 * p3[1] + col2 * p3[2] +
                V3{m[12], m[13], m[14]};
        V3 norm = normalize(inv0 * n3[0] + inv1 * n3[1] + inv2 * n3[2]);
        store3(v.position, pt);
        store3(v.normal, norm);
      }
      if (determinant < 0)
        for (size_t i = 0; i < count; i += 3)
          std::swap(expanded[i + 1], expanded[i + 2]);
      TangentInput input{&expanded};
      SMikkTSpaceInterface iface{};
      iface.m_getNumFaces = mikk_faces;
      iface.m_getNumVerticesOfFace = mikk_verts;
      iface.m_getPosition = mikk_pos;
      iface.m_getNormal = mikk_normal;
      iface.m_getTexCoord = mikk_uv;
      iface.m_setTSpaceBasic = mikk_set;
      SMikkTSpaceContext ctx{&iface, &input};
      if (!genTangSpaceDefault(&ctx))
        throw std::runtime_error("MikkTSpace tangent generation failed");
      kgfx_asset_primitive desc{};
      desc.first_index = uint32_t(indices.size());
      desc.index_count = uint32_t(count);
      desc.material_index = primitive.material
                                ? uint32_t(primitive.material - raw->materials)
                                : 0;
      std::map<std::array<uint32_t, 12>, uint32_t> unique;
      for (auto &v : expanded) {
        std::array<uint32_t, 12> key;
        std::memcpy(key.data(), &v, sizeof(v));
        auto [it, inserted] = unique.emplace(key, uint32_t(vertices.size()));
        if (inserted)
          vertices.push_back(v);
        indices.push_back(it->second);
      }
      primitives.push_back(desc);
    }
  }
  kgfx_model_file header{};
  std::memcpy(header.magic, "KGMODEL", 7);
  header.version = KGFX_ASSET_VERSION;
  header.material_count = uint32_t(materials.size());
  header.primitive_count = uint32_t(primitives.size());
  header.vertex_count = uint32_t(vertices.size());
  header.index_count = uint32_t(indices.size());
  header.materials_offset = sizeof(header);
  header.primitives_offset =
      header.materials_offset + materials.size() * sizeof(kgfx_asset_material);
  header.vertices_offset = header.primitives_offset +
                           primitives.size() * sizeof(kgfx_asset_primitive);
  header.indices_offset =
      header.vertices_offset + vertices.size() * sizeof(kgfx_asset_vertex);
  std::fill_n(header.bounds_min, 3, std::numeric_limits<float>::max());
  std::fill_n(header.bounds_max, 3, -std::numeric_limits<float>::max());
  for (auto &v : vertices)
    for (int c = 0; c < 3; ++c) {
      header.bounds_min[c] = std::min(header.bounds_min[c], v.position[c]);
      header.bounds_max[c] = std::max(header.bounds_max[c], v.position[c]);
    }
  Writer out(output);
  write(out.out, header);
  write_vec(out.out, materials);
  write_vec(out.out, primitives);
  write_vec(out.out, vertices);
  write_vec(out.out, indices);
  out.finish();
  std::cout << output.filename() << ": " << vertices.size() << " vertices, "
            << indices.size() / 3 << " triangles, " << primitives.size()
            << " primitives\n";
}
struct Environment {
  std::vector<Image> levels;
  V3 sample(V3 d, float lod = 0) const {
    float u = std::atan2(d.z, d.x) / (2 * pi) + .5f,
          v = std::acos(std::clamp(d.y, -1.f, 1.f)) / pi;
    auto lookup = [&](int level) {
      const auto &im = levels[size_t(level)];
      float fx = u * im.w - .5f, fy = v * im.h - .5f;
      int x = int(std::floor(fx)), y = int(std::floor(fy));
      float tx = fx - x, ty = fy - y;
      auto texel = [&](int a, int b) {
        a = (a % im.w + im.w) % im.w;
        b = std::clamp(b, 0, im.h - 1);
        return load3(im.rgba.data() + (size_t(b) * im.w + a) * 4);
      };
      return texel(x, y) * ((1 - tx) * (1 - ty)) +
             texel(x + 1, y) * (tx * (1 - ty)) +
             texel(x, y + 1) * ((1 - tx) * ty) +
             texel(x + 1, y + 1) * (tx * ty);
    };
    lod = std::clamp(lod, 0.f, float(levels.size() - 1));
    int a = int(lod), b = std::min(a + 1, int(levels.size() - 1));
    float t = lod - a;
    return lookup(a) * (1 - t) + lookup(b) * t;
  }
  float sample_lod(V3 d, float solid_angle) const {
    float sin_theta = std::sqrt(std::max(1e-5f, 1 - d.y * d.y));
    float texel_angle = (2 * pi / levels[0].w) * (pi / levels[0].h) * sin_theta;
    return std::max(0.f, .5f * std::log2(solid_angle / texel_angle));
  }
};
Environment read_environment(const fs::path &path) {
  float *p = nullptr;
  int w = 0, h = 0;
  const char *error = nullptr;
  int code = LoadEXR(&p, &w, &h, path.string().c_str(), &error);
  if (code != TINYEXR_SUCCESS) {
    std::string s = error ? error : "unknown EXR error";
    FreeEXRErrorMessage(error);
    throw std::runtime_error(s);
  }
  if (w != 2048 || h != 1024) {
    free(p);
    throw std::runtime_error("Expected 2K equirectangular EXR");
  }
  Environment env;
  Image im{w, h, std::vector<float>(p, p + size_t(w) * h * 4)};
  free(p);
  for (float v : im.rgba)
    if (!std::isfinite(v))
      throw std::runtime_error("Non-finite EXR radiance");
  env.levels.push_back(std::move(im));
  while (env.levels.back().w > 1 || env.levels.back().h > 1)
    env.levels.push_back(downsample_environment(env.levels.back()));
  return env;
}
V3 prefilter(const Environment &env, V3 n, float roughness, uint32_t samples) {
  if (roughness < 1e-6f)
    return env.sample(n);
  V3 sum{};
  float weight = 0;
  float a = roughness * roughness;
  for (uint32_t i = 0; i < samples; ++i) {
    V3 h = tangent_to_world(ggx(float(i) / samples, radical(i), roughness), n);
    float nh = std::max(0.f, dot(n, h));
    V3 l = h * (2 * nh) - n;
    float nl = std::max(0.f, dot(n, l));
    if (nl <= 0)
      continue;
    float den = nh * nh * (a * a - 1) + 1;
    float distribution = a * a / (pi * den * den);
    float pdf = std::max(1e-7f, distribution * .25f);
    float lod = env.sample_lod(l, 1 / (samples * pdf));
    sum = sum + env.sample(l, lod) * nl;
    weight += nl;
  }
  return weight > 0 ? sum * (1 / weight) : env.sample(n);
}
struct IrradianceSample {
  V3 direction;
  V3 weighted_radiance;
};
// Sample radiance rather than a changing cosine lobe. Reusing these world-space
// samples across cube texels keeps the convolution smooth between normals and
// allocates samples to the intense compact lights found in photographic HDRIs.
std::vector<IrradianceSample> diffuse_samples(const Environment &env,
                                              uint32_t count) {
  const Image &im = env.levels.front();
  std::vector<double> cdf(size_t(im.w) * im.h);
  double total = 0;
  for (int y = 0; y < im.h; ++y) {
    double area = (2 * std::numbers::pi / im.w) *
                  (std::cos(std::numbers::pi * y / im.h) -
                   std::cos(std::numbers::pi * (y + 1) / im.h));
    for (int x = 0; x < im.w; ++x) {
      size_t pixel = size_t(y) * im.w + x;
      const float *v = im.rgba.data() + pixel * 4;
      double luminance =
          std::max(0., .2126 * v[0] + .7152 * v[1] + .0722 * v[2]);
      total += luminance * area;
      cdf[pixel] = total;
    }
  }
  std::vector<IrradianceSample> result;
  result.reserve(count);
  if (total <= 0)
    return result;
  for (uint32_t i = 0; i < count; ++i) {
    double target = (double(i) + .5) * total / count;
    size_t pixel = std::min(
        cdf.size() - 1,
        size_t(std::lower_bound(cdf.begin(), cdf.end(), target) - cdf.begin()));
    int x = int(pixel % im.w), y = int(pixel / im.w);
    float longitude = 2 * pi * ((x + radical(i)) / im.w - .5f);
    double cy0 = std::cos(std::numbers::pi * y / im.h),
           cy1 = std::cos(std::numbers::pi * (y + 1) / im.h);
    float cy = float(cy0 + (cy1 - cy0) * radical(i ^ 0x9e3779b9u));
    float sy = std::sqrt(std::max(0.f, 1 - cy * cy));
    V3 radiance = load3(im.rgba.data() + pixel * 4);
    double luminance =
        .2126 * radiance.x + .7152 * radiance.y + .0722 * radiance.z;
    result.push_back({{sy * std::cos(longitude), cy, sy * std::sin(longitude)},
                      radiance * float(total / (luminance * pi * count))});
  }
  return result;
}
V3 irradiance(std::span<const IrradianceSample> samples, V3 normal) {
  V3 sum{};
  for (const auto &sample : samples)
    sum = sum + sample.weighted_radiance *
                    std::max(0.f, dot(normal, sample.direction));
  return sum;
}
std::array<float, 2> integrate_brdf(float nv, float roughness,
                                    uint32_t samples) {
  V3 v{std::sqrt(std::max(0.f, 1 - nv * nv)), 0, nv};
  double scale = 0, bias = 0;
  float a = roughness * roughness;
  for (uint32_t i = 0; i < samples; ++i) {
    V3 h = ggx(float(i) / samples, radical(i), roughness);
    float vh = std::max(0.f, dot(v, h));
    V3 l = h * (2 * vh) - v;
    float nl = std::max(0.f, l.z), nh = std::max(0.f, h.z);
    if (nl <= 0 || nh <= 0)
      continue;
    float gv = nl * std::sqrt(nv * nv * (1 - a * a) + a * a);
    float gl = nv * std::sqrt(nl * nl * (1 - a * a) + a * a);
    float visibility = .5f / std::max(gv + gl, 1e-7f);
    float g_vis = visibility * 4 * nl * vh / nh;
    float fc = std::pow(1 - vh, 5);
    scale += (1 - fc) * g_vis;
    bias += fc * g_vis;
  }
  return {float(scale / samples), float(bias / samples)};
}
std::vector<float> make_brdf(uint32_t size, uint32_t samples) {
  std::vector<float> result(size_t(size) * size * 4);
  parallel_rows(size, [&](size_t y) {
    for (uint32_t x = 0; x < size; ++x) {
      auto value = integrate_brdf((x + .5f) / size, (y + .5f) / size, samples);
      size_t p = (y * size + x) * 4;
      result[p] = value[0];
      result[p + 1] = value[1];
      result[p + 3] = 1;
    }
  });
  return result;
}
constexpr uint32_t specular_mip_count(uint32_t size) {
  uint32_t count = 1;
  // GGX roughness broadens the lobe but does not make a directional environment
  // constant per face. A 1x1/2x2 tail turns that smooth signal into visible bands.
  while (size > KGFX_ENVIRONMENT_MIN_SPECULAR_SIZE) {
    size /= 2;
    ++count;
  }
  return count;
}
void cook_environment(const fs::path &source, const fs::path &output) {
  Environment env = read_environment(source);
  constexpr uint32_t cube_size = 512, irr_size = 32, brdf_size = 256,
                     spec_samples = 1024, irr_samples = 32768,
                     brdf_samples = 1024;
  kgfx_environment_file header{};
  std::memcpy(header.magic, "KGENV", 5);
  header.version = KGFX_ASSET_VERSION;
  header.cube_size = cube_size;
  header.mip_count = specular_mip_count(cube_size);
  header.irradiance_size = irr_size;
  header.brdf_size = brdf_size;
  header.specular_offset = sizeof(header);
  uint64_t spec_pixels = 0;
  for (uint32_t mip = 0; mip < header.mip_count; ++mip) {
    uint32_t size = cube_size >> mip;
    spec_pixels += uint64_t(size) * size * 6;
  }
  header.irradiance_offset = header.specular_offset + spec_pixels * 16;
  header.brdf_offset =
      header.irradiance_offset + uint64_t(irr_size) * irr_size * 6 * 16;
  header.file_bytes = header.brdf_offset + uint64_t(brdf_size) * brdf_size * 16;
  Writer out(output);
  write(out.out, header);
  for (uint32_t mip = 0; mip < header.mip_count; ++mip) {
    uint32_t size = cube_size >> mip;
    float roughness = float(mip) / (header.mip_count - 1);
    std::vector<float> cube(size_t(size) * size * 6 * 4);
    parallel_rows(size_t(size) * 6, [&](size_t row) {
      int face = int(row / size), y = int(row % size);
      for (uint32_t x = 0; x < size; ++x) {
        V3 n = cube_direction(face, (x + .5f) / size, (y + .5f) / size);
        V3 c = prefilter(env, n, roughness, spec_samples);
        size_t p = (row * size + x) * 4;
        store3(cube.data() + p, c);
        cube[p + 3] = 1;
      }
    });
    write_vec(out.out, cube);
    std::cout << source.filename() << " specular mip " << mip << " (" << size
              << ")\n"
              << std::flush;
  }
  const auto diffuse_distribution = diffuse_samples(env, irr_samples);
  std::vector<float> diffuse(size_t(irr_size) * irr_size * 6 * 4);
  parallel_rows(irr_size * 6, [&](size_t row) {
    int face = int(row / irr_size), y = int(row % irr_size);
    for (uint32_t x = 0; x < irr_size; ++x) {
      V3 d = cube_direction(face, (x + .5f) / irr_size, (y + .5f) / irr_size);
      size_t p = (row * irr_size + x) * 4;
      store3(diffuse.data() + p, irradiance(diffuse_distribution, d));
      diffuse[p + 3] = 1;
    }
  });
  write_vec(out.out, diffuse);
  write_vec(out.out, make_brdf(brdf_size, brdf_samples));
  out.finish();
  std::cout << output.filename() << ": " << header.file_bytes << " bytes\n";
}
void self_test() {
  auto require = [](bool ok, const char *label) {
    if (!ok)
      throw std::runtime_error(label);
  };
  static_assert(sizeof(kgfx_asset_vertex) == 48);
  require(std::endian::native == std::endian::little,
          "Little-endian output required");
  Image checker{2, 2, {0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 1}};
  auto mip = downsample(checker, true, false);
  require(std::abs(mip.rgba[0] - .73535698f) < 1e-5f,
          "sRGB mips must filter in linear light");
  Environment env;
  Image constant{8, 4, std::vector<float>(128)};
  for (size_t i = 0; i < constant.rgba.size(); i += 4) {
    constant.rgba[i] = 2;
    constant.rgba[i + 1] = 3;
    constant.rgba[i + 2] = 4;
    constant.rgba[i + 3] = 1;
  }
  env.levels.push_back(constant);
  while (env.levels.back().w > 1)
    env.levels.push_back(downsample_environment(env.levels.back()));
  const auto constant_samples = diffuse_samples(env, 32768);
  for (int f = 0; f < 6; ++f) {
    V3 n = cube_direction(f, .5f, .5f);
    require(std::abs(dot(n, n) - 1) < 1e-6f, "Cube directions normalized");
    for (float roughness : {.1f, .5f, 1.f}) {
      V3 p = prefilter(env, n, roughness, 128);
      require(std::abs(p.x - 2) < 2e-5f && std::abs(p.z - 4) < 4e-5f,
              "GGX must preserve constant HDR energy");
    }
    V3 p = irradiance(constant_samples, n);
    require(std::abs(p.x - 2) < .002f && std::abs(p.z - 4) < .004f,
            "Diffuse integral/pi must preserve constant HDR energy");
  }
  Image transparent{2, 2, {1, 0, 0, 1, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0}};
  auto alpha_mip = downsample(transparent, true, false);
  require(std::abs(alpha_mip.rgba[0] - 1) < 1e-5f &&
              alpha_mip.rgba[2] < 1e-5f &&
              std::abs(alpha_mip.rgba[3] - .25f) < 1e-5f,
          "Transparent colors must not bleed into visible color mipmaps");
  Environment gradient;
  Image sky{128, 64, std::vector<float>(128 * 64 * 4)};
  for (int y = 0; y < sky.h; ++y)
    for (int x = 0; x < sky.w; ++x) {
      float latitude = std::cos(pi * (y + .5f) / sky.h);
      size_t p = (size_t(y) * sky.w + x) * 4;
      sky.rgba[p] = 1 + .5f * latitude;
      sky.rgba[p + 1] = latitude * latitude;
      sky.rgba[p + 2] = 1;
      sky.rgba[p + 3] = 1;
    }
  gradient.levels.push_back(sky);
  while (gradient.levels.back().w > 1)
    gradient.levels.push_back(downsample_environment(gradient.levels.back()));
  require(std::abs(gradient.levels.back().rgba[1] - 1.f / 3) < .001f,
          "Equirectangular mip tail must preserve spherical mean (cos² "
          "latitude integrates to 1/3)");
  const auto gradient_samples = diffuse_samples(gradient, 32768);
  for (V3 normal : {V3{0, 1, 0}, V3{0, -1, 0}, V3{1, 0, 0}}) {
    V3 integral = irradiance(gradient_samples, normal);
    require(std::abs(integral.x - (1 + normal.y / 3)) < .003f,
            "First-order spherical lighting must match analytic cosine "
            "convolution");
  }
  // For roughness=1, GGX with N=V reduces to cosine convolution. Reconstruct
  // L(d)=1+0.5*d.y through the actual coarsest cube level, rather than testing
  // only texel centers. A full chain ending at 1x1 fails this angular-error bound.
  const uint32_t tail_size = 512u >> (specular_mip_count(512) - 1);
  for (int face : {0, 2}) {
    std::vector<float> tail(size_t(tail_size) * tail_size);
    for (uint32_t y = 0; y < tail_size; ++y)
      for (uint32_t x = 0; x < tail_size; ++x)
        tail[size_t(y) * tail_size + x] =
            prefilter(gradient,
                      cube_direction(face, (x + .5f) / tail_size,
                                     (y + .5f) / tail_size),
                      1, 2048).x;
    for (uint32_t y = 0; y < 17; ++y)
      for (uint32_t x = 0; x < 17; ++x) {
        float u = .1f + .8f * x / 16, v = .1f + .8f * y / 16;
        float fx = u * tail_size - .5f, fy = v * tail_size - .5f;
        int ix = int(std::floor(fx)), iy = int(std::floor(fy));
        float tx = fx - ix, ty = fy - iy;
        auto tap = [&](int sx, int sy) {
          return tail[size_t(std::clamp(sy, 0, int(tail_size) - 1)) * tail_size +
                      std::clamp(sx, 0, int(tail_size) - 1)];
        };
        float actual = tap(ix, iy) * (1 - tx) * (1 - ty) +
                       tap(ix + 1, iy) * tx * (1 - ty) +
                       tap(ix, iy + 1) * (1 - tx) * ty +
                       tap(ix + 1, iy + 1) * tx * ty;
        float expected = 1 + cube_direction(face, u, v).y / 3;
        require(std::abs(actual - expected) < .01f,
                "Rough GGX cube must preserve smooth directional lighting "
                "between texels");
      }
  }
  auto b = integrate_brdf(1, .001f, 1024);
  require(std::abs(b[0] - 1) < .001f && b[1] < .0001f,
          "Mirror BRDF at normal incidence");
  std::cout << "Asset cooker tests passed: linear-light mipmaps, cube "
               "orientations, HDR energy, BRDF endpoints\n";
}
int main(int argc, char **argv) {
  try {
    if (argc == 2 && std::string(argv[1]) == "--self-test") {
      self_test();
      return 0;
    }
    if (argc != 4)
      throw std::runtime_error("Usage: kgfx_asset_cooker model|environment "
                               "INPUT OUTPUT or --self-test");
    if (std::endian::native != std::endian::little)
      throw std::runtime_error(
          "Asset writer currently requires a little-endian host");
    std::string command = argv[1];
    if (command == "model")
      cook_model(argv[2], argv[3]);
    else if (command == "environment")
      cook_environment(argv[2], argv[3]);
    else
      throw std::runtime_error("Unknown cooker command");
    return 0;
  } catch (const std::exception &e) {
    std::cerr << "asset cooker: " << e.what() << '\n';
    return 1;
  }
}
