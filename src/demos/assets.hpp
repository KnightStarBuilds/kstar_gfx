#pragma once
#include <kstar_gfx/gfx.h>
#include "math.hpp"
#include "../../tools/include/kgfx_asset_format.h"
#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace demo {
using Vertex=kgfx_asset_vertex;
struct Material {
    V4 base{1,1,1,1};V3 emissive{};
    float metallic=0,roughness=0.5f,normal_scale=1,cutoff=0,occlusion_strength=1;
    bool blend=false,double_sided=false;
    kgfx_texture *albedo=nullptr,*normal=nullptr,*orm=nullptr,*emission=nullptr;
};
struct Primitive {uint32_t first=0,count=0,material=0;};
struct Mesh {
    kgfx_buffer *vertices=nullptr,*indices=nullptr;
    uint32_t vertex_count=0,index_count=0;
    V3 lo{},hi{};
    std::vector<Primitive> primitives;
    std::vector<Material> materials;
};
struct Environment {kgfx_texture *specular=nullptr,*irradiance=nullptr,*brdf=nullptr;uint32_t mips=1;};
struct Resources {
    kgfx_device *device;
    std::vector<kgfx_buffer *> buffers;
    std::vector<kgfx_texture *> textures;
    std::vector<kgfx_sampler *> samplers;
    std::vector<kgfx_pipeline *> pipelines;
    std::map<std::filesystem::path,kgfx_texture *> texture_cache;
    explicit Resources(kgfx_device*d):device(d){}
    ~Resources();
    Resources(const Resources&)=delete;
    Resources&operator=(const Resources&)=delete;
    void check(kgfx_result,const char *operation)const;
    kgfx_buffer *buffer(size_t,uint32_t,const void *data=nullptr);
    kgfx_texture *texture(uint32_t,uint32_t,kgfx_format,uint32_t mips=1,uint32_t samples=1,uint32_t usage=KGFX_TEXTURE_SAMPLED);
    kgfx_sampler *sampler(bool mipmapped=true,kgfx_wrap wrap=KGFX_REPEAT,kgfx_filter filter=KGFX_LINEAR);
    kgfx_texture *load_texture(const std::filesystem::path&);
    Mesh load_model(const std::filesystem::path&);
    Environment load_environment(const std::filesystem::path&);
    void retire(kgfx_texture *);
    void retire(kgfx_buffer *);
};
std::vector<uint8_t> read_file(const std::filesystem::path&);
Mesh make_cube(Resources&);
Mesh make_sphere(Resources&,uint32_t slices=48,uint32_t stacks=32);
Mesh make_plane(Resources&);
}
