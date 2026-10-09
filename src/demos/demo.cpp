#include "demo.hpp"
#include "assets.hpp"
#if __has_include("scene.vert.h")
#include "scene.vert.h"
#endif
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string_view>

namespace demo {
namespace {
using Clock=std::chrono::steady_clock;
constexpr size_t max_draws=512;
struct alignas(16) Uniforms {
    M4 model,normal_matrix,view_projection,light_projection,inverse_view_projection;
    V4 camera,light_direction,light_color,base_color,material,options,emissive,environment;
};
static_assert(sizeof(Uniforms)==448);
#ifdef KGFX_SCENE_VERT_SPV_KGFX_U0_ASSERT_LAYOUT
KGFX_SCENE_VERT_SPV_KGFX_U0_ASSERT_LAYOUT(Uniforms);
#endif
struct Instance {V4 position_scale,color;};
struct Draw {
    const Mesh *mesh=nullptr;Material material;M4 transform=identity();
    uint32_t first=0,count=0,instances=1;bool instanced=false;
    float distance=0;
};
uint64_t integer(std::string_view s,const char*name){size_t n=0;uint64_t v;try{if(s.empty()||s.front()=='-')throw std::invalid_argument("negative");v=std::stoull(std::string(s),&n);}catch(...){throw std::runtime_error(std::string("Invalid ")+name);}if(n!=s.size())throw std::runtime_error(std::string("Invalid ")+name);return v;}
float real(std::string_view s,const char*name){size_t n=0;float v;try{v=std::stof(std::string(s),&n);}catch(...){throw std::runtime_error(std::string("Invalid ")+name);}if(n!=s.size()||!std::isfinite(v))throw std::runtime_error(std::string("Invalid ")+name);return v;}
double elapsed_ms(Clock::time_point a,Clock::time_point b){return std::chrono::duration<double,std::milli>(b-a).count();}
std::string json_escape(std::string_view s){std::string out;for(char c:s){if(c=='"'||c=='\\')out+='\\';if(static_cast<unsigned char>(c)>=32)out+=c;}return out;}
float srgb(float x){return x<=.0031308f?12.92f*x:1.055f*std::pow(x,1/2.4f)-.055f;}

struct Renderer {
    kgfx_device *device;Resources r;Options options;
    std::filesystem::path root;
    uint32_t width,height,environment_index=0,diagnostic=0;
    uint32_t presentation_width=0,presentation_height=0;
    float orbit=0.25f,elevation=.22f,distance=10.8f,exposure=1;
    V3 target{0,.4f,0},eye{};
    Mesh cube,sphere,plane,rifle,hat,stress_mesh;
    std::array<Environment,2> environments{};
    kgfx_texture *white=nullptr,*normal=nullptr,*orm=nullptr,*black=nullptr,*checker=nullptr;
    kgfx_texture *hdr=nullptr,*depth=nullptr,*resolved=nullptr,*ldr=nullptr,*shadow=nullptr,*shadow_copy=nullptr;
    kgfx_sampler *linear=nullptr,*clamp=nullptr,*mip_clamp=nullptr,*shadow_sampler=nullptr;
    kgfx_buffer *uniform=nullptr,*instances=nullptr,*triangle_vertices=nullptr;
    kgfx_pipeline *scene[2][2][2]{},*shadow_pipeline[2][2]{},*sky_pipeline=nullptr,*tone_pipeline=nullptr,*present_pipeline=nullptr,*triangle_pipeline=nullptr;
    size_t uniform_stride=0;std::vector<uint8_t> uniform_data;
    std::vector<Draw> draws;std::vector<Instance> instance_data;
    Uniforms global{};
    uint64_t frame=0;
    kgfx_texture *stress_texture=nullptr;kgfx_buffer *stress_buffer=nullptr;
    kgfx_stats stats{};
    Renderer(kgfx_device*d,const Options&o):device(d),r(d),options(o),root(o.resources),width(o.width),height(o.height),environment_index(o.environment),diagnostic(o.diagnostic),exposure(o.exposure){
        if(root.empty())throw std::runtime_error("The host must set the executable-relative resource directory");
        const auto*caps=kgfx_device_caps(device);
        if(!caps)throw std::runtime_error("Device has no capability record");
        if(options.scene!="triangle"&&(caps->max_samples<4||caps->max_texture_bindings<8||caps->max_texture_size<2048))throw std::runtime_error("The full-quality demos require 4x MSAA, 8 texture bindings and 2048px targets");
        uniform_stride=(sizeof(Uniforms)+std::max(16u,caps->uniform_alignment)-1)/std::max(16u,caps->uniform_alignment)*std::max(16u,caps->uniform_alignment);
        uniform_data.resize((max_draws+1)*uniform_stride);
        uniform=r.buffer(uniform_data.size(),KGFX_BUFFER_UNIFORM|KGFX_BUFFER_DYNAMIC);
        linear=r.sampler();clamp=r.sampler(false,KGFX_CLAMP);mip_clamp=r.sampler(true,KGFX_CLAMP);shadow_sampler=r.sampler(false,KGFX_CLAMP,KGFX_NEAREST);
        present_pipeline=pipeline("fullscreen.vert","present.frag",KGFX_RGBA8,1,false,false,false,false);
        if(options.scene=="triangle"){
            const float vertices[]={-.7f,-.65f,0,1,0,0,.7f,-.65f,0,0,1,0,0,.75f,0,0,0,1};
            triangle_vertices=r.buffer(sizeof(vertices),KGFX_BUFFER_VERTEX,vertices);
            triangle_pipeline=pipeline("triangle.vert","triangle.frag",KGFX_RGBA8,1,false,false,false,false,true);
            resize(width,height);return;
        }
        const uint8_t white_pixel[]={255,255,255,255},normal_pixel[]={128,128,255,255},black_pixel[]={0,0,0,255};
        white=solid(white_pixel,KGFX_SRGBA8);normal=solid(normal_pixel,KGFX_RGBA8);orm=solid(white_pixel,KGFX_RGBA8);black=solid(black_pixel,KGFX_SRGBA8);
        checker=checker_texture();
        environments[0]=constant_environment();environments[1]=environments[0];
        if(options.scene=="pbr"){
            const auto asset_root=root/"assets"/"cooked";
            rifle=r.load_model(asset_root/"bolt_action_rifle_7_62.kgmodel");hat=r.load_model(asset_root/"fishermans_hat.kgmodel");
            environments[0]=r.load_environment(asset_root/"industrial_pipe_and_valve_01.kgenv");environments[1]=r.load_environment(asset_root/"qwantani_night.kgenv");
        }
        cube=make_cube(r);sphere=make_sphere(r);plane=make_plane(r);
        draws.reserve(max_draws);instance_data.resize(1024);
        instances=r.buffer(instance_data.size()*sizeof(Instance),KGFX_BUFFER_VERTEX|KGFX_BUFFER_DYNAMIC);
        for(unsigned inst=0;inst<2;++inst)for(unsigned two=0;two<2;++two){
            shadow_pipeline[inst][two]=pipeline("shadow.vert","shadow.frag",KGFX_RGBA8,1,true,true,inst!=0,two!=0);
            for(unsigned blend=0;blend<2;++blend)scene[inst][two][blend]=pipeline("scene.vert","scene.frag",KGFX_RGBA16F,4,true,!blend,inst!=0,two!=0,false,blend!=0);
        }
        sky_pipeline=pipeline("fullscreen.vert","environment.frag",KGFX_RGBA16F,4,false,false,false,false);
        tone_pipeline=pipeline("fullscreen.vert","tonemap.frag",KGFX_RGBA8,1,false,false,false,false);
        shadow=r.texture(2048,2048,KGFX_DEPTH32F,1,1,KGFX_TEXTURE_SAMPLED|KGFX_TEXTURE_TARGET);
        if(options.scene=="offscreen")shadow_copy=r.texture(2048,2048,KGFX_DEPTH32F,1,1,KGFX_TEXTURE_SAMPLED|KGFX_TEXTURE_TARGET);
        if(options.scene=="instances"){distance=16;target={0,0,0};elevation=.65f;}
        if(options.scene=="pbr"&&options.view=="rifle"){target={-2,.05f,.6f};distance=3.1f;orbit=.02f;elevation=.15f;}
        if(options.scene=="pbr"&&options.view=="hat"){target={2.3f,-.18f,.4f};distance=2.6f;orbit=.45f;elevation=.32f;}
        resize(width,height);
    }
    std::vector<uint8_t> shader(const char*name){
#if defined(KSTAR_GFX_BACKEND_GNM)
        return read_file(root/"shaders"/(std::string(name)+".sb"));
#else
        const auto generated=root/"shaders"/(std::string(name)+".glsl");
        if(std::filesystem::exists(generated))return read_file(generated);
        // The source conversion supports early offline GL builds before host
        // compilers are acquired. It has no runtime shader reflection.
        auto bytes=read_file(root/"shaders"/name);std::string source(bytes.begin(),bytes.end());
        auto replace_all=[&](std::string_view a,std::string_view b){size_t p=0;while((p=source.find(a,p))!=std::string::npos){source.replace(p,a.size(),b);p+=b.size();}};
        replace_all("#version 450","#version 410 core");replace_all("layout(std140,binding=0)","layout(std140)");
        for(unsigned n=0;n<KGFX_MAX_TEXTURE_BINDINGS+8;++n)replace_all("layout(binding="+std::to_string(n)+") ","");
        replace_all("gl_VertexIndex","gl_VertexID");return {source.begin(),source.end()};
#endif
    }
    kgfx_pipeline *pipeline(const char*vs,const char*fs,kgfx_format color,uint32_t samples,bool depth_test,bool depth_write,bool instanced,bool two_sided,bool triangle=false,bool alpha=false){
        auto v=shader(vs),f=shader(fs);kgfx_pipeline_desc d{};d.vertex={v.data(),v.size()};d.fragment={f.data(),f.size()};d.color_format=color;d.depth_format=KGFX_DEPTH32F;d.samples=samples;d.depth_test=depth_test;d.depth_write=depth_write;d.depth_compare=KGFX_COMPARE_LESS;d.cull=two_sided||!depth_test?KGFX_CULL_NONE:KGFX_CULL_BACK;d.blend=alpha?KGFX_BLEND_ALPHA:KGFX_BLEND_OPAQUE;
        if(triangle){d.attribute_count=2;d.vertex_stride[0]=24;d.attributes[0]={0,0,0,0,KGFX_FLOAT3};d.attributes[1]={1,12,0,0,KGFX_FLOAT3};}
        else if(depth_test){d.attribute_count=4;d.vertex_stride[0]=sizeof(Vertex);d.attributes[0]={0,0,0,0,KGFX_FLOAT3};d.attributes[1]={1,12,0,0,KGFX_FLOAT3};d.attributes[2]={2,24,0,0,KGFX_FLOAT4};d.attributes[3]={3,40,0,0,KGFX_FLOAT2};if(instanced){d.attribute_count=6;d.vertex_stride[1]=sizeof(Instance);d.attributes[4]={4,0,1,1,KGFX_FLOAT4};d.attributes[5]={5,16,1,1,KGFX_FLOAT4};}}
        kgfx_pipeline*p=nullptr;r.check(kgfx_pipeline_create(device,&d,&p),(std::string("create pipeline ")+vs+" / "+fs).c_str());r.pipelines.push_back(p);return p;
    }
    kgfx_texture *solid(const uint8_t*pixel,kgfx_format format){auto*t=r.texture(1,1,format);r.check(kgfx_texture_upload(device,t,0,0,pixel,4),"upload default texture");return t;}
    kgfx_texture *checker_texture(){
        auto*t=r.texture(128,128,KGFX_SRGBA8,8);uint32_t dim=128;
        for(uint32_t mip=0;mip<8;++mip){std::vector<uint8_t>pixels(size_t(dim)*dim*4);for(uint32_t y=0;y<dim;++y)for(uint32_t x=0;x<dim;++x){float c=mip>4?.51f:(((x<<mip)/8+(y<<mip)/8)&1)?.82f:.20f;size_t a=(size_t(y)*dim+x)*4;pixels[a]=uint8_t(srgb(c)*255);pixels[a+1]=uint8_t(srgb(c*.82f)*255);pixels[a+2]=uint8_t(srgb(c*.5f)*255);pixels[a+3]=255;}r.check(kgfx_texture_upload(device,t,mip,0,pixels.data(),size_t(dim)*4),"upload checker mip");dim=std::max(1u,dim/2);}return t;
    }
    Environment constant_environment(){
        Environment e;e.specular=r.texture(1,1,KGFX_RGBA32F,1,1,KGFX_TEXTURE_SAMPLED|KGFX_TEXTURE_CUBE);e.irradiance=r.texture(1,1,KGFX_RGBA32F,1,1,KGFX_TEXTURE_SAMPLED|KGFX_TEXTURE_CUBE);e.brdf=r.texture(1,1,KGFX_RGBA32F);
        const float irradiance[]={.1f,.13f,.18f,1},specular[]={.05f,.07f,.1f,1},brdf[]={1,0,0,1};for(uint32_t face=0;face<6;++face){r.check(kgfx_texture_upload(device,e.specular,0,face,specular,16),"upload procedural cube");r.check(kgfx_texture_upload(device,e.irradiance,0,face,irradiance,16),"upload procedural irradiance");}r.check(kgfx_texture_upload(device,e.brdf,0,0,brdf,16),"upload procedural BRDF");return e;
    }
    void resize(uint32_t w,uint32_t h){
        if(!w||!h)return;
        for(auto*t:{hdr,depth,resolved,ldr})if(t)r.retire(t);
        width=w;height=h;r.check(kgfx_device_resize(device,w,h),"resize device");
        ldr=r.texture(w,h,KGFX_RGBA8,1,1,KGFX_TEXTURE_TARGET|KGFX_TEXTURE_SAMPLED);
        if(options.scene!="triangle"){hdr=r.texture(w,h,KGFX_RGBA16F,1,4,KGFX_TEXTURE_TARGET);depth=r.texture(w,h,KGFX_DEPTH32F,1,4,KGFX_TEXTURE_TARGET);resolved=r.texture(w,h,KGFX_RGBA16F,1,1,KGFX_TEXTURE_TARGET|KGFX_TEXTURE_SAMPLED);}
    }
    void input(const Input&i){
        if(i.presentation_width&&i.presentation_height){presentation_width=i.presentation_width;presentation_height=i.presentation_height;}
        if(i.reset){orbit=.25f;elevation=.22f;distance=10.8f;exposure=1;diagnostic=0;environment_index=0;}
        orbit+=i.orbit_x;elevation=std::clamp(elevation+i.orbit_y,-1.35f,1.35f);distance=std::clamp(distance*std::exp(-i.zoom*.1f),.5f,100.0f);exposure=std::clamp(exposure*std::exp2(i.exposure_stops),.001f,128.0f);
        if(i.next_environment)environment_index=1-environment_index;if(i.next_diagnostic)diagnostic=(diagnostic+1)%5;
        if(i.drawable_width&&i.drawable_height&&(i.drawable_width!=width||i.drawable_height!=height))resize(i.drawable_width,i.drawable_height);
    }
    void add(const Mesh&m,M4 transform,const Material&material,uint32_t first=0,uint32_t count=0,uint32_t instance_count=1,bool instanced=false){
        if(draws.size()>=max_draws)throw std::runtime_error("Demo draw arena capacity exceeded");
        V3 center{transform.v[12],transform.v[13],transform.v[14]};draws.push_back({&m,material,transform,first,count?count:m.index_count,instance_count,instanced,dot(center-eye,center-eye)});
    }
    void add_model(const Mesh&m,V3 center,float extent,float angle){
        V3 span=m.hi-m.lo,mid=(m.hi+m.lo)*.5f;float s=extent/std::max({span.x,span.y,span.z});M4 transform=translate(center)*rotate_y(angle)*scale({s,s,s})*translate(mid*-1);
        for(const auto&p:m.primitives)add(m,transform,m.materials[p.material],p.first,p.count);
    }
    void build_scene(){
        const float time=float(frame)/60.0f;
        eye=target+V3{std::sin(orbit)*std::cos(elevation),std::sin(elevation),std::cos(orbit)*std::cos(elevation)}*distance;
        global={};global.model=identity();global.view_projection=perspective(48*pi/180,float(width)/float(height),.05f,100)*look_at(eye,target,{0,1,0});global.inverse_view_projection=inverse(global.view_projection);
        global.light_projection=ortho(-8,8,-8,8,.1f,40)*look_at({-8,13,9},{0,0,0},{0,1,0});global.camera={eye.x,eye.y,eye.z,exposure};global.light_direction={-.45f,.75f,.5f,3.4f};global.light_color={1,.93f,.82f,1};global.base_color={1,1,1,1};global.material={0,.5f,0,1};global.options={float(diagnostic),time,0,0};global.environment={float(environments[environment_index].mips-1),1,0,0};
        draws.clear();Material floor;floor.base={.09f,.11f,.14f,1};floor.roughness=.72f;
        if(options.scene!="instances")add(plane,translate({0,-1.25f,0})*scale({18,1,18}),floor);
        if(options.scene=="pbr"){
            add_model(rifle,{-2,.05f,.6f},4.4f,-.1f);add_model(hat,{2.3f,-.18f,.4f},2.3f,.1f);
            for(unsigned y=0;y<2;++y)for(unsigned x=0;x<7;++x){Material m;m.base=y?V4{.8f,.49f,.17f,1}:V4{.16f,.35f,.52f,1};m.metallic=float(y);m.roughness=.06f+float(x)*.145f;add(sphere,translate({(float(x)-3)*.9f,float(y)*.9f-.25f,-2.2f})*scale({.36f,.36f,.36f}),m);}
        }else if(options.scene=="instances"){
            for(uint32_t n=0;n<instance_data.size();++n){uint32_t x=n%32,z=n/32;float fx=(float(x)-15.5f)*.4f,fz=(float(z)-15.5f)*.4f;instance_data[n]={{fx,std::sin(fx*.7f+time)*std::cos(fz*.5f+time)*.45f,fz,.24f},{.25f+.7f*float(x)/31,.3f+.6f*float(z)/31,.6f,1}};}
            r.check(kgfx_buffer_update(device,instances,0,instance_data.data(),instance_data.size()*sizeof(Instance)),"stream instance data");Material m;m.metallic=.55f;m.roughness=.25f;add(cube,identity(),m,0,0,uint32_t(instance_data.size()),true);
            // Ordered translucent sheets make blending behavior observable;
            // instanced opaque geometry remains one submission per pass.
            for(unsigned n=0;n<12;++n){Material sprite;sprite.base={.2f+float(n)*.05f,.25f,.9f,.28f};sprite.blend=true;sprite.double_sided=true;add(plane,translate({float(n)*.5f-2.75f,1.5f,1.5f-float(n)*.2f})*rotate_x(pi/2)*scale({1.1f,1,1.1f}),sprite);}
        }else if(options.scene=="offscreen"){
            for(unsigned z=0;z<3;++z)for(unsigned x=0;x<5;++x){Material m;m.base={.7f,.28f+.12f*float(z),.12f,1};m.metallic=float(z)*.5f;m.roughness=.08f+float(x)*.2f;add(sphere,translate({(float(x)-2)*1.25f,-.2f,(float(z)-1)*1.4f})*scale({.5f,.5f,.5f}),m);}
            Material glow;glow.base={.1f,.1f,.1f,1};glow.emissive={8,2,.3f};glow.emission=white;add(cube,translate({-3,1,-2})*rotate_y(time*.3f)*scale({.5f,.5f,.5f}),glow);
        }else{
            for(unsigned z=0;z<3;++z)for(unsigned x=0;x<5;++x){Material m;m.albedo=checker;m.metallic=float(z)*.35f;m.roughness=.18f+float(x)*.12f;add(cube,translate({(float(x)-2)*1.3f,-.15f,(float(z)-1)*1.45f})*rotate_y(time*.3f+float(x+z)) *scale({.85f,.85f,.85f}),m);}
            if(options.scene=="stress"){
                const uint8_t pixel[]={uint8_t(frame*13%255),uint8_t(frame*37%255),180,255};stress_texture=solid(pixel,KGFX_SRGBA8);
                const Vertex data[]={{{-.5f,0,-.5f},{0,1,0},{1,0,0,-1},{0,0}},{{.5f,0,-.5f},{0,1,0},{1,0,0,-1},{1,0}},{{-.5f,0,.5f},{0,1,0},{1,0,0,-1},{0,1}},{{.5f,0,.5f},{0,1,0},{1,0,0,-1},{1,1}}};
                stress_buffer=r.buffer(sizeof(data),KGFX_BUFFER_VERTEX|KGFX_BUFFER_DYNAMIC,data);stress_mesh.vertices=stress_buffer;stress_mesh.indices=plane.indices;stress_mesh.vertex_count=4;stress_mesh.index_count=6;
                Material m;m.albedo=stress_texture;m.roughness=.2f;m.double_sided=true;add(stress_mesh,translate({0,1.5f,0})*rotate_x(pi/2),m);
            }
        }
        // Stable insertion sort uses no temporary allocation and preserves
        // opaque order while placing transparent primitives back to front.
        auto before=[](const Draw&a,const Draw&b){if(a.material.blend!=b.material.blend)return !a.material.blend;return a.material.blend&&a.distance>b.distance;};
        for(size_t i=1;i<draws.size();++i){Draw v=draws[i];size_t j=i;while(j&&before(v,draws[j-1])){draws[j]=draws[j-1];--j;}draws[j]=v;}
        std::memcpy(uniform_data.data(),&global,sizeof(global));
        for(size_t n=0;n<draws.size();++n){auto u=global;const auto&d=draws[n];u.model=d.transform;u.normal_matrix=transpose(inverse(d.transform));u.base_color=d.material.base;u.material={d.material.metallic,d.material.roughness,d.material.cutoff,d.material.normal_scale};u.emissive={d.material.emissive.x,d.material.emissive.y,d.material.emissive.z,1};u.options.z=d.instanced?1.0f:0.0f;u.options.w=d.material.occlusion_strength;std::memcpy(uniform_data.data()+(n+1)*uniform_stride,&u,sizeof(u));}
        r.check(kgfx_buffer_update(device,uniform,0,uniform_data.data(),(draws.size()+1)*uniform_stride),"stream uniforms");
    }
    kgfx_bindings base_bindings(size_t uniform_slot=0){kgfx_bindings b{};b.uniforms[0]=uniform;b.uniform_offset[0]=uniform_slot*uniform_stride;b.uniform_size[0]=sizeof(Uniforms);return b;}
    void geometry(bool shadow_pass){
        auto&e=environments[environment_index];
        for(size_t n=0;n<draws.size();++n){const auto&d=draws[n];const auto&m=d.material;auto b=base_bindings(n+1);b.vertex[0]=d.mesh->vertices;b.vertex[1]=d.instanced?instances:nullptr;b.index=d.mesh->indices;b.index_size=4;
            b.textures[0]=m.albedo?m.albedo:white;b.samplers[0]=linear;
            if(!shadow_pass){b.textures[1]=m.normal?m.normal:normal;b.textures[2]=m.orm?m.orm:orm;b.textures[3]=m.emission?m.emission:black;b.textures[4]=e.irradiance;b.textures[5]=e.specular;b.textures[6]=e.brdf;b.textures[7]=shadow_copy?shadow_copy:shadow;for(unsigned i=1;i<4;++i)b.samplers[i]=linear;b.samplers[4]=clamp;b.samplers[5]=mip_clamp;b.samplers[6]=clamp;b.samplers[7]=shadow_sampler;}
            kgfx_bind_pipeline(device,shadow_pass?shadow_pipeline[d.instanced][m.double_sided]:scene[d.instanced][m.double_sided][m.blend]);kgfx_bind_resources(device,&b);kgfx_draw_indexed(device,d.first,d.count,d.instances);
        }
    }
    void begin(kgfx_texture*color,kgfx_texture*dep,uint32_t w,uint32_t h,const V4&clear){kgfx_pass_desc pass{};pass.color=color;pass.depth=dep;pass.width=w;pass.height=h;pass.color_load=KGFX_CLEAR;pass.depth_load=KGFX_CLEAR;pass.clear_color[0]=clear.x;pass.clear_color[1]=clear.y;pass.clear_color[2]=clear.z;pass.clear_color[3]=clear.w;pass.clear_depth=1;r.check(kgfx_pass_begin(device,&pass),"begin render pass");kgfx_viewport(device,0,0,w,h);}
    void fullscreen(kgfx_pipeline*p,kgfx_texture*texture,kgfx_sampler*s=nullptr){auto b=base_bindings();b.textures[0]=texture;b.samplers[0]=s?s:clamp;kgfx_bind_pipeline(device,p);kgfx_bind_resources(device,&b);kgfx_draw(device,0,3,1);}
    kgfx_completion render(bool capture,kgfx_readback**readback){
        if(options.scene=="stress"&&frame&&frame%60==0){const bool alternate=(frame/60)%2!=0;resize(options.width+(alternate?16:0),options.height+(alternate?16:0));}
        r.check(kgfx_frame_begin(device),"begin frame");
        if(options.scene=="triangle"){
            begin(ldr,nullptr,width,height,{.025f,.03f,.045f,1});kgfx_bindings b{};b.vertex[0]=triangle_vertices;kgfx_bind_pipeline(device,triangle_pipeline);kgfx_bind_resources(device,&b);kgfx_draw(device,0,3,1);kgfx_pass_end(device);
        }else{
            build_scene();r.check(kgfx_texture_transition(device,shadow,KGFX_ACCESS_ATTACHMENT),"shadow attachment transition");begin(nullptr,shadow,2048,2048,{0,0,0,0});geometry(true);kgfx_pass_end(device);r.check(kgfx_texture_transition(device,shadow,KGFX_ACCESS_SAMPLED),"shadow sample transition");
            if(shadow_copy){r.check(kgfx_texture_transition(device,shadow,KGFX_ACCESS_COPY_SOURCE),"depth copy source");r.check(kgfx_texture_transition(device,shadow_copy,KGFX_ACCESS_COPY_DESTINATION),"depth copy destination");r.check(kgfx_texture_copy(device,shadow,shadow_copy,0,0,0,0,2048,2048),"copy shadow depth");r.check(kgfx_texture_transition(device,shadow_copy,KGFX_ACCESS_SAMPLED),"copied depth sample transition");}
            begin(hdr,depth,width,height,{0,0,0,1});
            auto b=base_bindings();b.textures[5]=environments[environment_index].specular;b.samplers[5]=mip_clamp;kgfx_bind_pipeline(device,sky_pipeline);kgfx_bind_resources(device,&b);kgfx_draw(device,0,3,1);
            geometry(false);kgfx_pass_end(device);
            r.check(kgfx_texture_resolve(device,hdr,resolved),"resolve HDR MSAA");r.check(kgfx_texture_transition(device,resolved,KGFX_ACCESS_SAMPLED),"resolved sample transition");
            begin(ldr,nullptr,width,height,{0,0,0,1});fullscreen(tone_pipeline,resolved);kgfx_pass_end(device);
        }
        r.check(kgfx_texture_transition(device,ldr,KGFX_ACCESS_SAMPLED),"LDR sample transition");
        if(capture)r.check(kgfx_readback_begin(device,ldr,readback),"begin completed-GPU capture");
        if(!options.headless){begin(nullptr,nullptr,presentation_width?presentation_width:width,presentation_height?presentation_height:height,{0,0,0,1});fullscreen(present_pipeline,ldr);kgfx_pass_end(device);}
        // Retire after recording references but before submission. The stress
        // demo exercises pending-work retirement rather than immediate deletion.
        if(stress_texture){r.retire(stress_texture);stress_texture=nullptr;}if(stress_buffer){r.retire(stress_buffer);stress_buffer=nullptr;}
        auto completion=kgfx_submit(device,options.headless?0:1);kgfx_get_stats(device,&stats);++frame;return completion;
    }
    void capture(kgfx_readback*readback,kgfx_completion completion){
        r.check(kgfx_wait(device,completion,30'000'000'000ull),"wait for captured GPU frame");kgfx_readback_data data{};r.check(kgfx_readback_map(device,readback,&data),"map completed GPU capture");
        if(data.format!=KGFX_RGBA8||data.width!=width||data.height!=height||data.row_pitch<size_t(width)*4||data.size<data.row_pitch*height)throw std::runtime_error("Unexpected capture image layout");
        if(!options.output.parent_path().empty())std::filesystem::create_directories(options.output.parent_path());
        std::ofstream out(options.output,std::ios::binary);if(!out)throw std::runtime_error("Cannot create capture "+options.output.string());out<<"P6\n"<<width<<' '<<height<<"\n255\n";
        for(uint32_t y=0;y<height;++y){const auto*p=static_cast<const uint8_t*>(data.pixels)+size_t(y)*data.row_pitch;for(uint32_t x=0;x<width;++x)out.write(reinterpret_cast<const char*>(p+x*4),3);}
        if(!out)throw std::runtime_error("Failed to write GPU capture");kgfx_readback_destroy(device,readback);std::cout<<"Captured GPU frame "<<frame<<" to "<<options.output.string()<<'\n';
    }
};
}
const char *usage(){return
    "kstar_gfx_demo [--demo triangle|cubes|pbr|instances|offscreen|stress]\n"
    "  --width N --height N       drawable resolution (default 1920x1080)\n"
    "  --frames N                bounded run; fixed 1/60s scene step\n"
    "  --seconds N               visible timed session; capture final frame on exit\n"
    "  --capture-frame N         capture completed GPU output, 1-based\n"
    "  --output PATH.ppm         capture path; writes JSON metrics alongside\n"
    "  --headless                hidden native window, no presentation\n"
    "  --benchmark               disable vsync, report timings after 60 warmup frames\n"
    "  --resources PATH          packaged shader/assets root\n"
    "  --environment 0|1         industrial / Qwantani night\n"
    "  --diagnostic MODE         shaded|normals|albedo|material|shadows\n"
    "  --view overview|rifle|hat  fixed gallery camera for material inspection\n"
    "  --exposure VALUE          linear exposure multiplier\n"
    "Controls: drag left mouse to orbit; wheel zoom; E environment; [/] exposure;\n"
    "          D diagnostics; R reset camera; Escape quit.\n";
}
Options parse_options(int argc,char**argv){
    Options o;
    for(int i=1;i<argc;++i){std::string_view arg=argv[i],name=arg,value;auto eq=arg.find('=');if(eq!=std::string_view::npos){name=arg.substr(0,eq);value=arg.substr(eq+1);}auto val=[&](){if(eq!=std::string_view::npos)return value;if(i+1>=argc)throw std::runtime_error("Missing value for "+std::string(name));return std::string_view(argv[++i]);};
        if(name=="--demo")o.scene=val();else if(name=="--diagnostic"){const std::array names{"shaded","normals","albedo","material","shadows"};auto value=val();auto found=std::find(names.begin(),names.end(),value);if(found==names.end())throw std::runtime_error("Unknown diagnostic: "+std::string(value));o.diagnostic=uint32_t(found-names.begin());}else if(name=="--view")o.view=val();else if(name=="--frames")o.frames=integer(val(),"frame count");else if(name=="--seconds"){o.seconds=real(val(),"duration");if(o.seconds<=0)throw std::runtime_error("Duration must be positive");}else if(name=="--capture-frame")o.capture_frame=integer(val(),"capture frame");else if(name=="--width"){auto n=integer(val(),"width");if(n>16384)throw std::runtime_error("Width exceeds 16384");o.width=uint32_t(n);}else if(name=="--height"){auto n=integer(val(),"height");if(n>16384)throw std::runtime_error("Height exceeds 16384");o.height=uint32_t(n);}else if(name=="--output")o.output=val();else if(name=="--resources")o.resources=val();else if(name=="--environment"){auto n=integer(val(),"environment");if(n>1)throw std::runtime_error("Environment must be 0 or 1");o.environment=uint32_t(n);}else if(name=="--exposure")o.exposure=real(val(),"exposure");else if(name=="--headless"&&eq==std::string_view::npos)o.headless=true;else if(name=="--benchmark"&&eq==std::string_view::npos)o.benchmark=true;else if(name=="--help"||name=="-h")o.help=true;else throw std::runtime_error("Unknown argument: "+std::string(arg));
    }
    const std::array names{"triangle","cubes","pbr","instances","offscreen","stress"};if(std::find(names.begin(),names.end(),o.scene)==names.end())throw std::runtime_error("Unknown demo: "+o.scene);
    if(o.view!="overview"&&o.view!="rifle"&&o.view!="hat")throw std::runtime_error("View must be overview, rifle or hat");
    if(o.scene!="pbr"&&o.view!="overview")throw std::runtime_error("Close views require --demo pbr");
    if(!o.width||!o.height||o.exposure<=0)throw std::runtime_error("Dimensions and exposure must be positive");
    if(o.seconds&&o.frames)throw std::runtime_error("Choose --seconds or --frames");
    if(o.seconds<0||o.seconds>600)throw std::runtime_error("Duration must be within 0..600 seconds");
    if(o.capture_frame&&o.output.empty())throw std::runtime_error("--capture-frame requires --output");
    if(!o.output.empty()){if(o.output.extension()!=".ppm")throw std::runtime_error("Captures use the .ppm format");if(!o.capture_frame&&!o.seconds)o.capture_frame=o.frames?o.frames:1;if(o.frames&&o.capture_frame>o.frames)throw std::runtime_error("Capture frame exceeds frame limit");}
    if(o.headless&&!o.frames)throw std::runtime_error("A headless run requires --frames");return o;
}
int run(kgfx_device*device,const Options&options,const Host&host){
    try{
        Renderer renderer(device,options);std::vector<double> timings;timings.reserve(options.frames?size_t(std::min<uint64_t>(options.frames,1'000'000)):4096);bool captured=false;kgfx_completion last=0;uint64_t executed=0,steady_frame_allocations=0,steady_frames=0;
        std::cout<<"kstar_gfx "<<kgfx_backend_name()<<" / "<<kgfx_device_caps(device)->renderer<<" / "<<options.scene<<"\n";
        const auto began=Clock::now();uint64_t captured_frame=0;uint32_t captured_width=renderer.width,captured_height=renderer.height,captured_environment=renderer.environment_index,captured_diagnostic=renderer.diagnostic;float captured_exposure=renderer.exposure;
        while(!options.frames||executed<options.frames){Input input;const bool requested_quit=host.pump&&!host.pump(host.user,input);const bool finished=requested_quit||(options.seconds>0&&std::chrono::duration<double>(Clock::now()-began).count()>=options.seconds);
            // Timed interactive sessions render/capture one final presented frame
            // after quit. Deterministic frame-number captures retain their timing.
            const bool capture_on_exit=finished&&!captured&&!options.output.empty()&&!options.capture_frame;
            if(finished&&!capture_on_exit)break;
            renderer.input(input);kgfx_stats before{};kgfx_get_stats(device,&before);auto start=Clock::now();kgfx_readback*readback=nullptr;const bool capture=capture_on_exit||options.capture_frame==executed+1;last=renderer.render(capture,&readback);auto end=Clock::now();
            if(!capture&&options.scene!="stress"){steady_frame_allocations+=renderer.stats.steady_state_allocations-before.steady_state_allocations;++steady_frames;}
            if((!options.benchmark||executed>=60)&&!capture)timings.push_back(elapsed_ms(start,end));++executed;
            if(capture){renderer.capture(readback,last);captured=true;captured_frame=executed;captured_width=renderer.width;captured_height=renderer.height;captured_environment=renderer.environment_index;captured_diagnostic=renderer.diagnostic;captured_exposure=renderer.exposure;}
            if(host.title&&executed%30==0){char title[256];std::snprintf(title,sizeof(title),"kstar_gfx | %s | %ux%u | %.2f ms CPU | %llu draws | exposure %.2f | E env / D view / drag orbit",options.scene.c_str(),renderer.width,renderer.height,elapsed_ms(start,end),static_cast<unsigned long long>(renderer.stats.draws),renderer.exposure);host.title(host.user,title);}
            if(finished)break;
        }
        if(last)renderer.r.check(kgfx_wait(device,last,30'000'000'000ull),"wait for final frame");
        if(!options.output.empty()&&!captured)throw std::runtime_error("Run ended before the requested capture frame");
        std::sort(timings.begin(),timings.end());double total=0;for(double t:timings)total+=t;
        const double mean=timings.empty()?0:total/double(timings.size()),p50=timings.empty()?0:timings[timings.size()/2],p95=timings.empty()?0:timings[std::min(timings.size()-1,size_t(double(timings.size())*.95))];
        std::cout<<"frames="<<executed<<" CPU frame mean="<<mean<<"ms p50="<<p50<<"ms p95="<<p95<<"ms render_allocations="<<steady_frame_allocations<<" over "<<steady_frames<<" non-capture frames; total resource allocations="<<renderer.stats.steady_state_allocations<<'\n';
        if(!options.output.empty()){
            auto json_path=options.output;json_path.replace_extension(".json");std::ofstream j(json_path);if(!j)throw std::runtime_error("Cannot create capture metadata");
            j<<std::setprecision(9)<<"{\n  \"backend\": \""<<json_escape(kgfx_backend_name())<<"\",\n  \"renderer\": \""<<json_escape(kgfx_device_caps(device)->renderer)<<"\",\n  \"demo\": \""<<options.scene<<"\",\n  \"view\": \""<<options.view<<"\",\n  \"width\": "<<captured_width<<", \"height\": "<<captured_height<<",\n  \"frames\": "<<executed<<", \"capture_frame\": "<<captured_frame<<",\n  \"presentation_enabled\": "<<(options.headless?"false":"true")<<",\n  \"fixed_step_seconds\": 0.016666666666666666, \"seed\": 0,\n  \"environment\": "<<captured_environment<<", \"diagnostic\": "<<captured_diagnostic<<", \"exposure\": "<<captured_exposure<<",\n  \"quality\": {\"msaa\": "<<(options.scene=="triangle"?1:4)<<", \"shadow_size\": 2048, \"tone_map\": \"ACES fitted\"},\n  \"cpu_frame_ms\": {\"mean\": "<<mean<<", \"p50\": "<<p50<<", \"p95\": "<<p95<<", \"samples\": "<<timings.size()<<"},\n  \"stats\": {\"draws\": "<<renderer.stats.draws<<", \"instances\": "<<renderer.stats.instances<<", \"upload_bytes\": "<<renderer.stats.upload_bytes<<", \"steady_state_allocations\": "<<renderer.stats.steady_state_allocations<<", \"steady_frame_allocations\": "<<steady_frame_allocations<<", \"steady_frames\": "<<steady_frames<<"},\n  \"capture_source\": \"completed GPU LDR render target\"\n}\n";if(!j)throw std::runtime_error("Cannot write capture metadata");
        }
        return 0;
    }catch(const std::exception&e){std::cerr<<"kstar_gfx_demo: "<<e.what()<<'\n';return 1;}
}
}
