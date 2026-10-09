#include "assets.hpp"
#include <algorithm>
#include <cstring>
#include <fstream>
#include <limits>
#include <stdexcept>

namespace demo {
std::vector<uint8_t> read_file(const std::filesystem::path &path){
    std::ifstream f(path,std::ios::binary|std::ios::ate);
    if(!f)throw std::runtime_error("Cannot open "+path.string());
    auto len=f.tellg();if(len<0||uint64_t(len)>2ull*1024*1024*1024)throw std::runtime_error("Invalid file length: "+path.string());
    std::vector<uint8_t> bytes(static_cast<size_t>(len));
    f.seekg(0);if(!f.read(reinterpret_cast<char *>(bytes.data()),std::streamsize(bytes.size())))throw std::runtime_error("Cannot read "+path.string());return bytes;
}
namespace {
void bounds(const std::vector<uint8_t>&bytes,uint64_t offset,uint64_t size){if(offset>bytes.size()||size>bytes.size()-offset)throw std::runtime_error("Truncated cooked asset");}
template<class T>T record(const std::vector<uint8_t>&b,uint64_t off=0){bounds(b,off,sizeof(T));T v;std::memcpy(&v,b.data()+off,sizeof(T));return v;}
void magic(const char *actual,const char *expected,uint32_t version){if(std::memcmp(actual,expected,8)||version!=KGFX_ASSET_VERSION)throw std::runtime_error("Unsupported cooked asset format/version");}
std::filesystem::path texture_path(const std::filesystem::path&root,const char *str){
    if(!std::memchr(str,0,KGFX_ASSET_TEXTURE_PATH))throw std::runtime_error("Invalid cooked texture path");
    std::filesystem::path p(str);if(p.is_absolute())throw std::runtime_error("Cooked texture path must be relative");
    for(const auto&part:p)if(part=="..")throw std::runtime_error("Cooked texture path escapes asset directory");
    return root/p;
}
Mesh upload_mesh(Resources&r,const std::vector<Vertex>&v,const std::vector<uint32_t>&i){Mesh m;m.vertices=r.buffer(v.size()*sizeof(Vertex),KGFX_BUFFER_VERTEX,v.data());m.indices=r.buffer(i.size()*4,KGFX_BUFFER_INDEX,i.data());m.vertex_count=uint32_t(v.size());m.index_count=uint32_t(i.size());m.primitives.push_back({0,m.index_count,0});m.materials.emplace_back();return m;}
Vertex vertex(V3 p,V3 n,V4 t,V2 uv){return {{p.x,p.y,p.z},{n.x,n.y,n.z},{t.x,t.y,t.z,t.w},{uv.x,uv.y}};}
}
Resources::~Resources(){
    kgfx_device_wait_idle(device);
    for(auto*p:pipelines)kgfx_pipeline_destroy(device,p);
    for(auto*p:samplers)kgfx_sampler_destroy(device,p);
    for(auto*p:textures)kgfx_texture_destroy(device,p);
    for(auto*p:buffers)kgfx_buffer_destroy(device,p);
}
void Resources::check(kgfx_result result,const char*op)const{if(result!=KGFX_OK)throw std::runtime_error(std::string(op)+": "+kgfx_device_error(device)+" ("+std::to_string(result)+")");}
kgfx_buffer *Resources::buffer(size_t size,uint32_t usage,const void*data){kgfx_buffer*p=nullptr;kgfx_buffer_desc d{size,usage,data};check(kgfx_buffer_create(device,&d,&p),"create buffer");buffers.push_back(p);return p;}
kgfx_texture *Resources::texture(uint32_t w,uint32_t h,kgfx_format format,uint32_t mips,uint32_t samples,uint32_t usage){kgfx_texture*p=nullptr;kgfx_texture_desc d{w,h,mips,samples,usage,format};auto status=kgfx_texture_create(device,&d,&p);if(status!=KGFX_OK){const std::string operation="create texture "+std::to_string(w)+"x"+std::to_string(h)+" format="+std::to_string(format)+" mips="+std::to_string(mips)+" samples="+std::to_string(samples)+" usage="+std::to_string(usage);check(status,operation.c_str());}textures.push_back(p);return p;}
kgfx_sampler *Resources::sampler(bool mipmapped,kgfx_wrap wrap,kgfx_filter filter){kgfx_sampler*p=nullptr;kgfx_sampler_desc d{filter,filter,wrap,wrap,uint32_t(mipmapped),0,KGFX_COMPARE_ALWAYS};check(kgfx_sampler_create(device,&d,&p),"create sampler");samplers.push_back(p);return p;}
void Resources::retire(kgfx_texture*p){auto it=std::find(textures.begin(),textures.end(),p);if(it!=textures.end()){kgfx_texture_destroy(device,p);textures.erase(it);}}
void Resources::retire(kgfx_buffer*p){auto it=std::find(buffers.begin(),buffers.end(),p);if(it!=buffers.end()){kgfx_buffer_destroy(device,p);buffers.erase(it);}}
kgfx_texture *Resources::load_texture(const std::filesystem::path&path){
    if(auto it=texture_cache.find(path);it!=texture_cache.end())return it->second;
    auto b=read_file(path);auto h=record<kgfx_texture_file>(b);magic(h.magic,"KGTEX\0\0",h.version);
    if(!h.width||!h.height||!h.mip_count||h.mip_count>KGFX_ASSET_MAX_MIPS||(h.format!=KGFX_ASSET_RGBA8&&h.format!=KGFX_ASSET_RGBA32F))throw std::runtime_error("Invalid texture description");
    bounds(b,h.data_offset,h.data_bytes);const uint32_t stride=h.format==KGFX_ASSET_RGBA8?4:16;
    auto*p=texture(h.width,h.height,h.format==KGFX_ASSET_RGBA32F?KGFX_RGBA32F:(h.srgb?KGFX_SRGBA8:KGFX_RGBA8),h.mip_count);
    uint64_t offset=h.data_offset;uint32_t w=h.width,height=h.height;
    for(uint32_t mip=0;mip<h.mip_count;++mip){uint64_t bytes=uint64_t(w)*height*stride;bounds(b,offset,bytes);check(kgfx_texture_upload(device,p,mip,0,b.data()+offset,size_t(w)*stride),"upload cooked texture");offset+=bytes;w=std::max(1u,w/2);height=std::max(1u,height/2);}
    if(offset!=h.data_offset+h.data_bytes)throw std::runtime_error("Cooked texture byte count mismatch");
    texture_cache.emplace(path,p);return p;
}
Mesh Resources::load_model(const std::filesystem::path&path){
    auto bytes=read_file(path);auto h=record<kgfx_model_file>(bytes);magic(h.magic,"KGMODEL",h.version);
    if(!h.vertex_count||!h.index_count||!h.material_count||!h.primitive_count)throw std::runtime_error("Empty cooked model");
    bounds(bytes,h.vertices_offset,uint64_t(h.vertex_count)*sizeof(Vertex));bounds(bytes,h.indices_offset,uint64_t(h.index_count)*4);
    bounds(bytes,h.materials_offset,uint64_t(h.material_count)*sizeof(kgfx_asset_material));bounds(bytes,h.primitives_offset,uint64_t(h.primitive_count)*sizeof(kgfx_asset_primitive));
    Mesh m;m.lo={h.bounds_min[0],h.bounds_min[1],h.bounds_min[2]};m.hi={h.bounds_max[0],h.bounds_max[1],h.bounds_max[2]};
    m.vertices=buffer(size_t(h.vertex_count)*sizeof(Vertex),KGFX_BUFFER_VERTEX,bytes.data()+h.vertices_offset);m.indices=buffer(size_t(h.index_count)*4,KGFX_BUFFER_INDEX,bytes.data()+h.indices_offset);m.vertex_count=h.vertex_count;m.index_count=h.index_count;
    for(uint32_t n=0;n<h.material_count;++n){auto a=record<kgfx_asset_material>(bytes,h.materials_offset+uint64_t(n)*sizeof(kgfx_asset_material));Material mat;
        mat.base={a.base_color[0],a.base_color[1],a.base_color[2],a.base_color[3]};mat.emissive={a.emissive[0],a.emissive[1],a.emissive[2]};mat.metallic=a.metallic;mat.roughness=a.roughness;mat.normal_scale=a.normal_scale;mat.cutoff=a.alpha_mode==KGFX_ASSET_ALPHA_MASK?a.alpha_cutoff:0;
        mat.blend=a.alpha_mode==KGFX_ASSET_ALPHA_BLEND;mat.double_sided=a.double_sided!=0;mat.occlusion_strength=a.occlusion_strength;
        const auto root=path.parent_path();if(a.base_color_texture[0])mat.albedo=load_texture(texture_path(root,a.base_color_texture));if(a.normal_texture[0])mat.normal=load_texture(texture_path(root,a.normal_texture));if(a.material_texture[0])mat.orm=load_texture(texture_path(root,a.material_texture));if(a.emissive_texture[0])mat.emission=load_texture(texture_path(root,a.emissive_texture));m.materials.push_back(mat);
    }
    for(uint32_t n=0;n<h.primitive_count;++n){auto p=record<kgfx_asset_primitive>(bytes,h.primitives_offset+uint64_t(n)*sizeof(kgfx_asset_primitive));if(p.material_index>=m.materials.size()||p.first_index>m.index_count||p.index_count>m.index_count-p.first_index)throw std::runtime_error("Invalid cooked primitive");m.primitives.push_back({p.first_index,p.index_count,p.material_index});}
    for(uint32_t n=0;n<h.index_count;++n)if(record<uint32_t>(bytes,h.indices_offset+uint64_t(n)*4)>=h.vertex_count)throw std::runtime_error("Cooked mesh index exceeds vertex count");
    return m;
}
Environment Resources::load_environment(const std::filesystem::path&path){
    auto bytes=read_file(path);auto h=record<kgfx_environment_file>(bytes);magic(h.magic,"KGENV\0\0",h.version);
    if(!h.cube_size||!h.irradiance_size||!h.brdf_size||!h.mip_count||h.mip_count>KGFX_ASSET_MAX_MIPS||h.file_bytes!=bytes.size())throw std::runtime_error("Invalid environment dimensions");
    Environment env;env.mips=h.mip_count;
    env.specular=texture(h.cube_size,h.cube_size,KGFX_RGBA32F,h.mip_count,1,KGFX_TEXTURE_SAMPLED|KGFX_TEXTURE_CUBE);
    uint64_t offset=h.specular_offset;uint32_t size=h.cube_size;
    for(uint32_t mip=0;mip<h.mip_count;++mip){for(uint32_t face=0;face<6;++face){const uint64_t count=uint64_t(size)*size*16;bounds(bytes,offset,count);check(kgfx_texture_upload(device,env.specular,mip,face,bytes.data()+offset,size_t(size)*16),"upload specular cube");offset+=count;}size=std::max(1u,size/2);}
    env.irradiance=texture(h.irradiance_size,h.irradiance_size,KGFX_RGBA32F,1,1,KGFX_TEXTURE_SAMPLED|KGFX_TEXTURE_CUBE);offset=h.irradiance_offset;
    for(uint32_t face=0;face<6;++face){const uint64_t count=uint64_t(h.irradiance_size)*h.irradiance_size*16;bounds(bytes,offset,count);check(kgfx_texture_upload(device,env.irradiance,0,face,bytes.data()+offset,size_t(h.irradiance_size)*16),"upload irradiance cube");offset+=count;}
    env.brdf=texture(h.brdf_size,h.brdf_size,KGFX_RGBA32F);bounds(bytes,h.brdf_offset,uint64_t(h.brdf_size)*h.brdf_size*16);check(kgfx_texture_upload(device,env.brdf,0,0,bytes.data()+h.brdf_offset,size_t(h.brdf_size)*16),"upload BRDF table");return env;
}
Mesh make_cube(Resources&r){
    std::vector<Vertex>v;std::vector<uint32_t>i;
    const V3 normals[]={{0,0,1},{0,0,-1},{1,0,0},{-1,0,0},{0,1,0},{0,-1,0}};
    const V3 tangents[]={{1,0,0},{-1,0,0},{0,0,-1},{0,0,1},{1,0,0},{1,0,0}};
    for(int f=0;f<6;++f){V3 n=normals[f],t=tangents[f],b=cross(n,t);uint32_t first=uint32_t(v.size());for(int y=0;y<2;++y)for(int x=0;x<2;++x){V3 p=n*.5f+t*(float(x)-.5f)+b*(float(y)-.5f);v.push_back(vertex(p,n,{t.x,t.y,t.z,1},{float(x),float(y)}));}i.insert(i.end(),{first,first+1,first+2,first+2,first+1,first+3});}
    return upload_mesh(r,v,i);
}
Mesh make_sphere(Resources&r,uint32_t slices,uint32_t stacks){
    std::vector<Vertex>v;std::vector<uint32_t>i;
    for(uint32_t y=0;y<=stacks;++y){float phi=float(y)/float(stacks)*pi;for(uint32_t x=0;x<=slices;++x){float theta=float(x)/float(slices)*2*pi;V3 n{std::sin(phi)*std::cos(theta),std::cos(phi),std::sin(phi)*std::sin(theta)};v.push_back(vertex(n,n,{-std::sin(theta),0,std::cos(theta),1},{float(x)/float(slices),float(y)/float(stacks)}));}}
    for(uint32_t y=0;y<stacks;++y)for(uint32_t x=0;x<slices;++x){uint32_t a=y*(slices+1)+x,b=a+slices+1;i.insert(i.end(),{a,a+1,b,a+1,b+1,b});}
    return upload_mesh(r,v,i);
}
Mesh make_plane(Resources&r){std::vector<Vertex>v;for(int z=0;z<2;++z)for(int x=0;x<2;++x)v.push_back(vertex({float(x)-.5f,0,float(z)-.5f},{0,1,0},{1,0,0,-1},{float(x),float(z)}));return upload_mesh(r,v,{0,2,1,1,2,3});}
}
