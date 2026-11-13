#include "demo.hpp"
#include "assets.hpp"
#include "graphics_settings.hpp"
#include "overlay.hpp"
#include "showcase_world.hpp"
#include "showcase_renderer.hpp"
#include <kgfx_scene_catalog.hpp>
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
#include <memory>
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

struct TimingSummary { double mean=0,p50=0,p95=0;size_t samples=0;uint64_t total=0; };
// Retain the newest bounded sample window. Preallocation also covers indefinite
// interactive sessions, whose former growing vector allocated during rendering.
struct TimingSamples {
    static constexpr size_t capacity=65536;
    std::vector<double> values=std::vector<double>(capacity);
    uint64_t count=0;
    void add(double ms){values[size_t(count++%capacity)]=ms;}
    TimingSummary summarize(){
        const size_t n=size_t(std::min<uint64_t>(count,capacity));
        if(!n)return {};
        std::sort(values.begin(),values.begin()+n);
        double sum=0;for(size_t i=0;i<n;++i)sum+=values[i];
        return {sum/double(n),values[n/2],values[std::min(n-1,size_t(double(n)*.95))],n,count};
    }
};
struct GpuFrameTimers {
    kgfx_device*device;
    std::array<kgfx_gpu_timer*,8> timers{};
    std::array<bool,8> pending{};
    bool supported=false;
    size_t cursor=0,recording=8;
    uint64_t skipped=0;
    explicit GpuFrameTimers(kgfx_device*d):device(d){
        for(auto&timer:timers){
            const auto result=kgfx_gpu_timer_create(d,&timer);
            if(result==KGFX_UNSUPPORTED)break;
            if(result!=KGFX_OK){for(auto*owned:timers)kgfx_gpu_timer_destroy(d,owned);throw std::runtime_error("Cannot create GPU frame timer");}
            supported=true;
        }
    }
    ~GpuFrameTimers(){for(auto*timer:timers)kgfx_gpu_timer_destroy(device,timer);}
    void poll(TimingSamples&samples){
        if(!supported)return;
        for(size_t i=0;i<timers.size();++i)if(pending[i]){
            kgfx_gpu_timer_result value{};
            const auto result=kgfx_gpu_timer_read(device,timers[i],&value);
            if(result==KGFX_OK){samples.add(double(value.elapsed_ns)*1e-6);pending[i]=false;}
            else if(result!=KGFX_NOT_READY)throw std::runtime_error("Cannot read GPU frame timer");
        }
    }
    void begin(bool sample){
        recording=timers.size();
        if(!supported||!sample)return;
        // A slow GPU may fill the ring; omit a sample instead of waiting.
        if(pending[cursor]){++skipped;return;}
        if(kgfx_gpu_timer_begin(device,timers[cursor])!=KGFX_OK)throw std::runtime_error("Cannot begin GPU frame timer");
        recording=cursor;cursor=(cursor+1)%timers.size();
    }
    void end(){
        if(recording==timers.size())return;
        if(kgfx_gpu_timer_end(device,timers[recording])!=KGFX_OK)throw std::runtime_error("Cannot end GPU frame timer");
        pending[recording]=true;recording=timers.size();
    }
};

struct RenderTargets {
    Resources resources;
    kgfx_texture *hdr=nullptr,*depth=nullptr,*resolved=nullptr,*ldr=nullptr,*shadow=nullptr,*shadow_copy=nullptr;
    RenderTargets(kgfx_device*d,const GraphicsSettings&s,bool triangle,bool copy_shadow):resources(d){
        ldr=resources.texture(s.width,s.height,KGFX_RGBA8,1,1,KGFX_TEXTURE_TARGET|KGFX_TEXTURE_SAMPLED);
        if(triangle)return;
        hdr=resources.texture(s.width,s.height,KGFX_RGBA16F,1,s.samples,KGFX_TEXTURE_TARGET|(s.samples==1?KGFX_TEXTURE_SAMPLED:0));
        depth=resources.texture(s.width,s.height,KGFX_DEPTH32F,1,s.samples,KGFX_TEXTURE_TARGET);
        if(s.samples>1)resolved=resources.texture(s.width,s.height,KGFX_RGBA16F,1,1,KGFX_TEXTURE_TARGET|KGFX_TEXTURE_SAMPLED);
        shadow=resources.texture(s.shadow_size,s.shadow_size,KGFX_DEPTH32F,1,1,KGFX_TEXTURE_SAMPLED|KGFX_TEXTURE_TARGET);
        if(copy_shadow)shadow_copy=resources.texture(s.shadow_size,s.shadow_size,KGFX_DEPTH32F,1,1,KGFX_TEXTURE_SAMPLED|KGFX_TEXTURE_TARGET);
    }
};

struct Renderer {
    kgfx_device *device;Resources r;Options options;
    std::filesystem::path root;
    uint32_t width,height,environment_index=0,diagnostic=0;
    uint32_t presentation_width=0,presentation_height=0;
    GraphicsSettings settings{};
    GraphicsMenu menu;
    Overlay overlay;
    bool overlay_dirty=true,quit_requested=false,follow_window=true;
    std::unique_ptr<RenderTargets> targets;
    std::unique_ptr<ShowcaseWorld> world;
    std::unique_ptr<ShowcaseRenderer> showcase;
    std::string requested_scene;
    kgfx_completion last_completion=0;
    float orbit=0.25f,elevation=.22f,distance=10.8f,exposure=1;
    V3 target{0,.4f,0},eye{};
    Mesh cube,sphere,plane,rifle,hat,stress_mesh;
    Material ground_material;
    std::array<Environment,2> environments{};
    kgfx_texture *white=nullptr,*normal=nullptr,*orm=nullptr,*black=nullptr,*checker=nullptr;
    kgfx_texture *hdr=nullptr,*depth=nullptr,*resolved=nullptr,*ldr=nullptr,*shadow=nullptr,*shadow_copy=nullptr;
    kgfx_sampler *linear=nullptr,*clamp=nullptr,*mip_clamp=nullptr,*shadow_sampler=nullptr;
    kgfx_buffer *uniform=nullptr,*instances=nullptr,*triangle_vertices=nullptr,*overlay_vertices=nullptr;
    kgfx_pipeline *scene[2][2][2][2]{},*shadow_pipeline[2][2]{},*sky_pipeline[2]{},*tone_pipeline=nullptr,*present_pipeline=nullptr,*triangle_pipeline=nullptr,*overlay_pipeline=nullptr;
    size_t uniform_stride=0;std::vector<uint8_t> uniform_data;
    std::vector<Draw> draws;std::vector<Instance> instance_data;
    Uniforms global{};
    uint64_t frame=0;
    kgfx_texture *stress_texture=nullptr;kgfx_buffer *stress_buffer=nullptr;
    kgfx_stats stats{};
    double frame_begin_ms=0,submission_ms=0;
    bool match_requested=false;
    Renderer(kgfx_device*d,const Options&o):device(d),r(d),options(o),root(o.resources),width(o.width),height(o.height),environment_index(o.environment),diagnostic(o.diagnostic),exposure(o.exposure){
        if(root.empty())throw std::runtime_error("The host must set the executable-relative resource directory");
        const auto*caps=kgfx_device_caps(device);
        if(!caps)throw std::runtime_error("Device has no capability record");
        settings={width,height,o.samples,o.shadow_size,o.effects};
        menu.scene_count=uint32_t(kgfx_demo_catalog::scenes.size());
        menu.diagnostic_count=ShowcaseWorld::supports_scene(options.scene)?14:5;
        presentation_width=width;presentation_height=height;
        if(options.scene!="triangle"&&(caps->max_samples<4||caps->max_texture_bindings<8||caps->max_texture_size<2048))throw std::runtime_error("The full-quality demos require 4x MSAA, 8 texture bindings and 2048px targets");
        uniform_stride=(sizeof(Uniforms)+std::max(16u,caps->uniform_alignment)-1)/std::max(16u,caps->uniform_alignment)*std::max(16u,caps->uniform_alignment);
        uniform_data.resize((max_draws+1)*uniform_stride);
        uniform=r.buffer(uniform_data.size(),KGFX_BUFFER_UNIFORM|KGFX_BUFFER_DYNAMIC);
        linear=r.sampler();clamp=r.sampler(false,KGFX_CLAMP);mip_clamp=r.sampler(true,KGFX_CLAMP);shadow_sampler=r.sampler(false,KGFX_CLAMP,KGFX_NEAREST);
        present_pipeline=pipeline("fullscreen.vert","present.frag",KGFX_RGBA8,1,false,false,false,false);
        if(!options.headless){
            overlay_pipeline=pipeline("triangle.vert","triangle.frag",KGFX_RGBA8,1,false,false,false,false,true);
            overlay_vertices=r.buffer(Overlay::capacity*sizeof(OverlayVertex),KGFX_BUFFER_VERTEX|KGFX_BUFFER_DYNAMIC);
        }
        if(ShowcaseWorld::supports_scene(options.scene)){
            follow_window=false;
            world=std::make_unique<ShowcaseWorld>(options.scene,options.seed,particle_limit(settings),options.scene!="lighting_baked");
            world->set_view(options.view);world->set_sequence(options.sequence);
            if((options.scene=="lighting_baked"||options.scene=="lighting_dynamic")&&!world->load_geometry((root/"assets/cooked").string().c_str(),false))
                throw std::runtime_error("Industrial lighting geometry is missing or invalid; rebuild the cooked model resources");
            if(options.scene=="lighting_baked"&&!world->load_bake((root/"assets/cooked/lighting/industrial.kglight").string().c_str()))
                throw std::runtime_error("Lighting bake is missing or stale; rebuild the cooked industrial lighting resources");
            showcase=std::make_unique<ShowcaseRenderer>(device,root,settings);
            return;
        }
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
        ground_material.base={.09f,.11f,.14f,1};ground_material.roughness=.72f;
        if(options.scene=="pbr"){
            const auto asset_root=root/"assets"/"cooked";
            rifle=r.load_model(asset_root/"bolt_action_rifle_7_62.kgmodel");hat=r.load_model(asset_root/"fishermans_hat.kgmodel");
            environments[0]=r.load_environment(asset_root/"industrial_pipe_and_valve_01.kgenv");environments[1]=r.load_environment(asset_root/"qwantani_night.kgenv");
            ground_material.base={1,1,1,1};ground_material.roughness=1;ground_material.metallic=1;
            ground_material.albedo=r.load_texture(asset_root/"textures/rock_tile_floor_diff_2k.kgtex");
            ground_material.normal=r.load_texture(asset_root/"textures/rock_tile_floor_nor_gl_2k.kgtex");
            ground_material.orm=r.load_texture(asset_root/"textures/rock_tile_floor_arm_2k.kgtex");
        }
        // Rock Tile Floor covers two metres per tile: nine repeats across the
        // gallery's 18-metre plane. Repeat sampling retains the full mip chain.
        cube=make_cube(r);sphere=make_sphere(r);plane=make_plane(r,options.scene=="pbr"?9.0f:1.0f);
        draws.reserve(max_draws);instance_data.resize(1024);
        instances=r.buffer(instance_data.size()*sizeof(Instance),KGFX_BUFFER_VERTEX|KGFX_BUFFER_DYNAMIC);
        for(unsigned inst=0;inst<2;++inst)for(unsigned two=0;two<2;++two){
            shadow_pipeline[inst][two]=pipeline("shadow.vert","shadow.frag",KGFX_RGBA8,1,true,true,inst!=0,two!=0);
            for(unsigned aa=0;aa<2;++aa)for(unsigned blend=0;blend<2;++blend)scene[aa][inst][two][blend]=pipeline("scene.vert","scene.frag",KGFX_RGBA16F,aa?4:1,true,!blend,inst!=0,two!=0,false,blend!=0);
        }
        for(unsigned aa=0;aa<2;++aa)sky_pipeline[aa]=pipeline("fullscreen.vert","environment.frag",KGFX_RGBA16F,aa?4:1,false,false,false,false);
        tone_pipeline=pipeline("fullscreen.vert","tonemap.frag",KGFX_RGBA8,1,false,false,false,false);
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
    void replace_targets(const GraphicsSettings&next){
        if(last_completion)r.check(kgfx_wait(device,last_completion,30'000'000'000ull),"wait before changing graphics settings");
        if(showcase){showcase->resize(next);world->set_particle_limit(particle_limit(next));width=next.width;height=next.height;settings=next;overlay_dirty=true;return;}
        // Allocate the full candidate first. Failure destroys only the candidate;
        // all active textures and settings stay valid. VideoOut never resizes.
        auto candidate=std::make_unique<RenderTargets>(device,next,options.scene=="triangle",options.scene=="offscreen");
        targets.swap(candidate);
        hdr=targets->hdr;depth=targets->depth;resolved=targets->resolved;ldr=targets->ldr;
        shadow=targets->shadow;shadow_copy=targets->shadow_copy;
        width=next.width;height=next.height;settings=next;overlay_dirty=true;
    }
    void resize(uint32_t w,uint32_t h){
        if(!w||!h)return;
        auto next=settings;next.width=w;next.height=h;replace_targets(next);
    }
    void close_menu(){menu.visible=false;overlay_dirty=true;std::puts("GFX_MENU state=closed");}
    static uint32_t particle_limit(const GraphicsSettings&s){return s.effects==2?16384:s.effects==1?4096:8192;}
    uint32_t scene_index()const{for(size_t n=0;n<kgfx_demo_catalog::scenes.size();++n)if(kgfx_demo_catalog::scenes[n].id==options.scene)return uint32_t(n);return 0;}
    void apply_settings(){
        try{
            if(menu.pending!=settings)replace_targets(menu.pending);
            environment_index=menu.pending_environment;exposure=menu.pending_exposure;diagnostic=menu.pending_diagnostic;
            if(menu.pending_scene!=scene_index())requested_scene=kgfx_demo_catalog::scenes[menu.pending_scene].id;
            follow_window=false;menu.status="APPLIED - SETTINGS ACTIVE.";
            std::printf("GFX_SETTINGS width=%u height=%u samples=%u shadows=%u environment=%u diagnostic=%u exposure=%.3f\n",width,height,settings.samples,settings.shadow_size,environment_index,diagnostic,exposure);
        }catch(const std::exception&e){
            menu.status="APPLY FAILED - PREVIOUS SETTINGS KEPT.";
            std::fprintf(stderr,"GFX_SETTINGS rejected: %s\n",e.what());
        }
        overlay_dirty=true;
    }
    void input(const Input&i,double dt){
        if(i.presentation_width&&i.presentation_height&&(presentation_width!=i.presentation_width||presentation_height!=i.presentation_height)){presentation_width=i.presentation_width;presentation_height=i.presentation_height;overlay_dirty=true;}
        if(i.drawable_width&&i.drawable_height&&follow_window&&(i.drawable_width!=width||i.drawable_height!=height))resize(i.drawable_width,i.drawable_height);
        if(i.menu_toggle){
            if(menu.visible)close_menu();else {menu.open(settings,environment_index,exposure,diagnostic);menu.pending_scene=scene_index();overlay_dirty=true;std::puts("GFX_MENU state=open");}
            if(world)world->advance(0,{},true);
            return;
        }
        if(menu.visible){
            if(world)world->advance(0,{},true);
            if(i.menu_back){close_menu();return;}
            if(i.menu_vertical){menu.move(i.menu_vertical);overlay_dirty=true;}
            if(i.menu_horizontal){menu.adjust(i.menu_horizontal);overlay_dirty=true;}
            if(i.menu_accept){
                if(menu.row==GraphicsMenu::apply)apply_settings();
                else if(menu.row==GraphicsMenu::back)close_menu();
                else if(menu.row==GraphicsMenu::quit){quit_requested=true;std::puts("GFX_QUIT reason=menu");}
                else {menu.adjust(1);overlay_dirty=true;}
            }
            return;
        }
        // Hidden windows never acquire focus, and bounded visible captures can
        // lose it to another validation window. Both must keep fixed stepping.
        if(!i.focused&&!options.frames&&!options.headless){if(world)world->advance(0,{},true);return;}
        if(i.scene_delta){const int count=int(kgfx_demo_catalog::scenes.size());requested_scene=kgfx_demo_catalog::scenes[size_t((int(scene_index())+i.scene_delta+count)%count)].id;return;}
        if(i.matched_lighting&&(options.scene=="lighting_baked"||options.scene=="lighting_dynamic")){
            requested_scene=options.scene=="lighting_baked"?"lighting_dynamic":"lighting_baked";match_requested=true;return;
        }
        if(world){
            WorldInput movement;
            // Inspection consumes the legacy orbit controls; the same left
            // stick must not translate its target while it rotates the camera.
            if(!world->state().inspection){movement.move_forward=i.move_z;movement.move_right=i.move_x;movement.sprint=i.sprint;}
            movement.look_x=world->state().inspection?-i.orbit_x:i.look_x;
            movement.look_y=world->state().inspection?i.orbit_y:i.look_y;
            movement.zoom=i.zoom;movement.toggle_inspection=i.toggle_inspection;
            movement.reset=i.walk_reset||(world->state().inspection&&i.reset);
            movement.interact=i.interact&&!movement.reset;
            movement.toggle_animation=i.toggle_animation;movement.cycle_light_color=i.cycle_light_color;
            movement.light_intensity_delta=i.light_intensity_delta;
            if(options.frames||options.headless){
                if(options.ticks_per_frame==1)world->tick(movement);
                else world->advance(double(options.ticks_per_frame)/60.,movement);
            }else world->advance(dt,movement);
            if(i.next_environment)environment_index=1-environment_index;
            if(i.walkthrough_diagnostic||(world->state().inspection&&i.next_diagnostic))diagnostic=(diagnostic+1)%14;
            exposure=std::clamp(exposure*std::exp2(i.exposure_stops),.001f,128.0f);
            overlay_dirty=true;
            return;
        }
        if(i.reset){orbit=.25f;elevation=.22f;distance=10.8f;exposure=1;diagnostic=0;environment_index=0;}
        orbit+=i.orbit_x;elevation=std::clamp(elevation+i.orbit_y,-1.35f,1.35f);distance=std::clamp(distance*std::exp(-i.zoom*.1f),.5f,100.0f);exposure=std::clamp(exposure*std::exp2(i.exposure_stops),.001f,128.0f);
        if(i.next_environment)environment_index=1-environment_index;if(i.next_diagnostic)diagnostic=(diagnostic+1)%5;
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
        draws.clear();
        if(options.scene!="instances")add(plane,translate({0,-1.25f,0})*scale({18,1,18}),ground_material);
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
            kgfx_bind_pipeline(device,shadow_pass?shadow_pipeline[d.instanced][m.double_sided]:scene[settings.samples==4][d.instanced][m.double_sided][m.blend]);kgfx_bind_resources(device,&b);kgfx_draw_indexed(device,d.first,d.count,d.instances);
        }
    }
    void update_overlay(){
        constexpr std::array<float,3> white{.9f,.93f,.98f},muted{.6f,.67f,.76f},gold{1,.76f,.3f};
        overlay.vertices.clear();
        char text[160];
        if(!menu.visible){
            overlay.rect(48,946,1760,88,{.025f,.033f,.05f});
            std::snprintf(text,sizeof(text),"%s | %s | %uX%u | MSAA %uX",options.scene.c_str(),mode_name(graphics_mode(settings)),width,height,options.scene=="triangle"?1:settings.samples);
            overlay.text(70,965,text,white,3);
#if defined(KSTAR_GFX_BACKEND_GNM)
            overlay.text(70,999,world?"STICKS MOVE/LOOK  CROSS INTERACT  CIRCLE CAMERA  D-PAD SCENES  OPTIONS MENU":"OPTIONS SETTINGS    TRIANGLE ENVIRONMENT    L1 / R1 EXPOSURE    D-PAD SCENES",muted,2);
#else
            overlay.text(70,999,world?"WASD MOVE  F INTERACT  C CAMERA  V DIAGNOSTICS  PGUP/PGDN SCENES  ESC MENU":"ESC / F1 SETTINGS    E ENVIRONMENT    [ / ] EXPOSURE    PGUP / PGDN SCENES",muted,2);
#endif
            if(world){const auto prompt=world->interaction_prompt();if(!prompt.empty()){overlay.rect(48,872,1760,52,{.025f,.033f,.05f});overlay.text(70,888,prompt,gold,3);}}
            if(world&&(options.scene=="lighting_baked"||options.scene=="lighting_dynamic")){
                overlay.rect(48,792,1760,66,{.025f,.033f,.05f});
#if defined(KSTAR_GFX_BACKEND_GNM)
                overlay.text(70,804,"HOLD L3: CIRCLE MATCH   SQUARE FREEZE   TRIANGLE COLOR   L1 / R1 INTENSITY",muted,2);
#else
                overlay.text(70,804,"M MATCHED SCENE   P FREEZE   L LIGHT COLOR   - / + INTENSITY   R RESET",muted,2);
#endif
                const auto&s=world->state();std::snprintf(text,sizeof(text),"ANIMATION %s   STATIONARY INTENSITY %.2F   COLOR %u",s.animation_frozen?"FROZEN":"RUNNING",s.stationary_intensity,s.light_color_variant);overlay.text(70,830,text,gold,2);
            }
            if(showcase&&diagnostic==13){
                const auto&timing=showcase->timings();
                overlay.rect(48,48,1090,350,{.025f,.033f,.05f});
                overlay.text(70,70,timing.gpu_supported?"PASS TIMINGS / CPU RECORDING AND ASYNC GPU":"PASS TIMINGS / GPU TIMESTAMPS UNSUPPORTED",gold,2);
                std::snprintf(text,sizeof(text),"FRAME BEGIN %.3F MS   RECORD / SUBMIT %.3F MS",frame_begin_ms,submission_ms);overlay.text(70,104,text,muted,2);
                for(size_t pass=0;pass<ShowcaseTimings::count;++pass){
                    if(timing.gpu_valid[pass])std::snprintf(text,sizeof(text),"%s   CPU %.3F MS   GPU %.3F MS",ShowcaseTimings::names[pass],timing.cpu_ms[pass],timing.gpu_ms[pass]);
                    else std::snprintf(text,sizeof(text),"%s   CPU %.3F MS   GPU %s",ShowcaseTimings::names[pass],timing.cpu_ms[pass],timing.gpu_supported?"PENDING":"UNSUPPORTED");
                    overlay.text(70,143+float(pass)*32,text,white,2);
                }
            }
        }else{
            overlay.rect(48,48,1144,920,{.025f,.033f,.05f});
            overlay.rect(48,48,1144,5,gold);
            overlay.text(86,88,"KSTAR GFX  /  GRAPHICS",white,4);
            std::snprintf(text,sizeof(text),"ACTIVE: %uX%u  MSAA %uX  SHADOWS %u",width,height,settings.samples,settings.shadow_size);
            overlay.text(86,140,text,muted,3);
            std::snprintf(text,sizeof(text),"OUTPUT: %uX%u",presentation_width,presentation_height);
            overlay.text(86,173,text,muted,2);
            constexpr const char*labels[]{"MODE","RENDER RESOLUTION","ANTI-ALIASING","SHADOW RESOLUTION","ENVIRONMENT","EXPOSURE","DIAGNOSTIC","SCENE","APPLY CHANGES","BACK TO DEMO","QUIT APP"};
            constexpr const char*diagnostics[]{"SHADED","NORMALS","ALBEDO","MATERIAL","SHADOWS","LINEAR DEPTH","REFRACTION LAYERS","LAYER OVERFLOW","BAKED LIGHT","DYNAMIC BOUNCE","WETNESS","ROUGHNESS","PROBES","PASS TIMINGS"};
            for(int row=0;row<GraphicsMenu::row_count;++row){
                const float y=199+float(row)*48;
                if(row==menu.row){overlay.rect(70,y-11,1084,44,{.12f,.15f,.21f});overlay.text(86,y,">",gold,3);}
                overlay.text(124,y,labels[row],row==menu.row?gold:white,3);
                switch(row){
                case GraphicsMenu::mode:std::snprintf(text,sizeof(text),"%s",mode_name(menu.pending_mode));break;
                case GraphicsMenu::resolution:std::snprintf(text,sizeof(text),"%uX%u",menu.pending.width,menu.pending.height);break;
                case GraphicsMenu::antialiasing:std::snprintf(text,sizeof(text),"%s",menu.pending.samples==4?"4X MSAA":"OFF (1X)");break;
                case GraphicsMenu::shadows:std::snprintf(text,sizeof(text),"%u",menu.pending.shadow_size);break;
                case GraphicsMenu::environment:std::snprintf(text,sizeof(text),"%s",menu.pending_environment?"QWANTANI NIGHT":"INDUSTRIAL");break;
                case GraphicsMenu::exposure:std::snprintf(text,sizeof(text),"%+.2f EV",std::log2(menu.pending_exposure));break;
                case GraphicsMenu::diagnostic:std::snprintf(text,sizeof(text),"%s",diagnostics[menu.pending_diagnostic]);break;
                case GraphicsMenu::scene:std::snprintf(text,sizeof(text),"%s",kgfx_demo_catalog::scenes[menu.pending_scene].title.data());break;
                default:text[0]=0;break;
                }
                overlay.text(638,y,text,row==menu.row?gold:white,3);
            }
            overlay.text(86,754,menu.status,gold,2);
            overlay.text(86,802,"PERFORMANCE: 720P / 4X MSAA / 1024 SHADOWS",muted,2);
            overlay.text(86,829,"QUALITY: 1080P / 4X MSAA / 2048 SHADOWS",muted,2);
            overlay.text(86,856,"CINEMATIC: 1080P / FULL EFFECTS / 30 FPS TARGET",muted,2);
#if defined(KSTAR_GFX_BACKEND_GNM)
            overlay.text(86,904,"D-PAD EDIT   CROSS SELECT   CIRCLE / OPTIONS BACK",white,2);
#else
            overlay.text(86,904,"ARROWS EDIT   ENTER SELECT   BACKSPACE / ESC BACK",white,2);
#endif
        }
        overlay_dirty=false;
    }
    void draw_overlay(){
        if(overlay_dirty)update_overlay();
        // Dynamic buffers have one backing per frame in flight on both
        // backends. Refresh the current backing even while menu geometry is
        // unchanged; otherwise two frame slots retain the previous HUD.
        r.check(kgfx_buffer_update(device,overlay_vertices,0,overlay.vertices.data(),overlay.vertices.size()*sizeof(OverlayVertex)),"upload menu overlay");
        kgfx_bindings b{};b.vertex[0]=overlay_vertices;
        kgfx_bind_pipeline(device,overlay_pipeline);kgfx_bind_resources(device,&b);kgfx_draw(device,0,uint32_t(overlay.vertices.size()),1);
    }
    void begin(kgfx_texture*color,kgfx_texture*dep,uint32_t w,uint32_t h,const V4&clear){kgfx_pass_desc pass{};pass.color=color;pass.depth=dep;pass.width=w;pass.height=h;pass.color_load=KGFX_CLEAR;pass.depth_load=KGFX_CLEAR;pass.clear_color[0]=clear.x;pass.clear_color[1]=clear.y;pass.clear_color[2]=clear.z;pass.clear_color[3]=clear.w;pass.clear_depth=1;r.check(kgfx_pass_begin(device,&pass),"begin render pass");kgfx_viewport(device,0,0,w,h);}
    void fullscreen(kgfx_pipeline*p,kgfx_texture*texture,kgfx_sampler*s=nullptr){auto b=base_bindings();b.textures[0]=texture;b.samplers[0]=s?s:clamp;kgfx_bind_pipeline(device,p);kgfx_bind_resources(device,&b);kgfx_draw(device,0,3,1);}
    kgfx_completion render(bool capture,kgfx_readback**readback,GpuFrameTimers&timers,bool sample){
        if(options.scene=="stress"&&frame&&frame%60==0){const bool alternate=(frame/60)%2!=0;resize(options.width+(alternate?16:0),options.height+(alternate?16:0));}
        const auto before_begin=Clock::now();
        r.check(kgfx_frame_begin(device),"begin frame");
        const auto recording_begin=Clock::now();frame_begin_ms=elapsed_ms(before_begin,recording_begin);
        timers.begin(sample);
        if(showcase){
            world->update_lighting();
            ldr=showcase->render(world->frame(options.frames||options.headless?1:world->interpolation()),environment_index,diagnostic,exposure);
        }else if(options.scene=="triangle"){
            begin(ldr,nullptr,width,height,{.025f,.03f,.045f,1});kgfx_bindings b{};b.vertex[0]=triangle_vertices;kgfx_bind_pipeline(device,triangle_pipeline);kgfx_bind_resources(device,&b);kgfx_draw(device,0,3,1);kgfx_pass_end(device);
        }else{
            build_scene();r.check(kgfx_texture_transition(device,shadow,KGFX_ACCESS_ATTACHMENT),"shadow attachment transition");begin(nullptr,shadow,settings.shadow_size,settings.shadow_size,{0,0,0,0});geometry(true);kgfx_pass_end(device);r.check(kgfx_texture_transition(device,shadow,KGFX_ACCESS_SAMPLED),"shadow sample transition");
            if(shadow_copy){r.check(kgfx_texture_transition(device,shadow,KGFX_ACCESS_COPY_SOURCE),"depth copy source");r.check(kgfx_texture_transition(device,shadow_copy,KGFX_ACCESS_COPY_DESTINATION),"depth copy destination");r.check(kgfx_texture_copy(device,shadow,shadow_copy,0,0,0,0,settings.shadow_size,settings.shadow_size),"copy shadow depth");r.check(kgfx_texture_transition(device,shadow_copy,KGFX_ACCESS_SAMPLED),"copied depth sample transition");}
            begin(hdr,depth,width,height,{0,0,0,1});
            auto b=base_bindings();b.textures[5]=environments[environment_index].specular;b.samplers[5]=mip_clamp;kgfx_bind_pipeline(device,sky_pipeline[settings.samples==4]);kgfx_bind_resources(device,&b);kgfx_draw(device,0,3,1);
            geometry(false);kgfx_pass_end(device);
            auto*tone_source=hdr;
            if(settings.samples>1){r.check(kgfx_texture_resolve(device,hdr,resolved),"resolve HDR MSAA");tone_source=resolved;}
            r.check(kgfx_texture_transition(device,tone_source,KGFX_ACCESS_SAMPLED),"HDR sample transition");
            begin(ldr,nullptr,width,height,{0,0,0,1});fullscreen(tone_pipeline,tone_source);kgfx_pass_end(device);
        }
        r.check(kgfx_texture_transition(device,ldr,KGFX_ACCESS_SAMPLED),"LDR sample transition");
        if(capture)r.check(kgfx_readback_begin(device,ldr,readback),"begin completed-GPU capture");
        if(!options.headless){begin(nullptr,nullptr,presentation_width?presentation_width:width,presentation_height?presentation_height:height,{0,0,0,1});fullscreen(present_pipeline,ldr);draw_overlay();kgfx_pass_end(device);}
        // Retire after recording references but before submission. The stress
        // demo exercises pending-work retirement rather than immediate deletion.
        if(stress_texture){r.retire(stress_texture);stress_texture=nullptr;}if(stress_buffer){r.retire(stress_buffer);stress_buffer=nullptr;}
        timers.end();
        last_completion=kgfx_submit(device,options.headless?0:1);submission_ms=elapsed_ms(recording_begin,Clock::now());kgfx_get_stats(device,&stats);++frame;return last_completion;
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
    "kstar_gfx_demo [--demo ID] [--list-scenes]\n"
    "  --demo ID                 facility (default), glass_lab, waterworks, boiler_room,\n"
    "                            triangle, cubes, pbr, instances, offscreen, stress\n"
    "  --preset cinematic|quality|performance\n"
    "  --width N --height N       override preset render dimensions\n"
    "  --samples 1|4 --shadow-size 1024|2048\n"
    "  --seed N --sequence idle|interact|tour\n"
    "  --view NAME               overview; showcase glass/water/fire; pbr rifle/hat\n"
    "  --frames N                bounded run; fixed 1/60s simulation step\n"
    "  --ticks-per-frame 1|2     bounded walkthrough simulation budget (default 1)\n"
    "  --seconds N               visible timed session (maximum 600 seconds)\n"
    "  --capture-frame N --output PATH.ppm\n"
    "  --headless --benchmark --resources PATH\n"
    "  --environment 0|1 --exposure VALUE\n"
    "  --diagnostic shaded|normals|albedo|material|shadows|depth|layers|overflow\n"
    "               baked|bounce|wetness|roughness|probes|timers\n"
    "Walkthrough: WASD/mouse, F interact, C inspect, R reset, V diagnostics.\n"
    "Lighting: M matched scene, P freeze animation, L color, +/- intensity.\n"
    "All scenes: Page Up/Down scenes, E environment, [/] exposure, Escape/F1 menu.\n"
    "Legacy/inspection: mouse drag orbit, wheel zoom, D diagnostics.\n";
}
void print_scenes(){
    for(const auto&scene:kgfx_demo_catalog::scenes){
        std::cout<<scene.id<<" - "<<scene.title<<"\n  views:";
        for(auto v:scene.views)std::cout<<' '<<v;
        std::cout<<"; sequences:";for(auto v:scene.sequences)std::cout<<' '<<v;std::cout<<'\n';
    }
}
Options parse_options(int argc,char**argv){
    Options o;o.scene=kgfx_demo_catalog::default_scene;
    bool explicit_width=false,explicit_height=false,explicit_samples=false,explicit_shadow=false,explicit_exposure=false;
    for(int i=1;i<argc;++i){
        std::string_view arg=argv[i],name=arg,value;auto eq=arg.find('=');
        if(eq!=std::string_view::npos){name=arg.substr(0,eq);value=arg.substr(eq+1);}
        auto val=[&](){if(eq!=std::string_view::npos)return value;if(i+1>=argc)throw std::runtime_error("Missing value for "+std::string(name));return std::string_view(argv[++i]);};
        if(name=="--demo")o.scene=val();
        else if(name=="--preset")o.preset=val();
        else if(name=="--view")o.view=val();
        else if(name=="--sequence")o.sequence=val();
        else if(name=="--seed"){auto n=integer(val(),"seed");if(n>UINT32_MAX)throw std::runtime_error("Seed exceeds uint32");o.seed=uint32_t(n);}
        else if(name=="--diagnostic"){
            const std::array names{"shaded","normals","albedo","material","shadows","depth","layers","overflow","baked","bounce","wetness","roughness","probes","timers"};
            auto v=val();auto found=std::find(names.begin(),names.end(),v);
            if(found==names.end())throw std::runtime_error("Unknown diagnostic: "+std::string(v));o.diagnostic=uint32_t(found-names.begin());
        }
        else if(name=="--frames")o.frames=integer(val(),"frame count");
        else if(name=="--ticks-per-frame"){auto n=integer(val(),"ticks per frame");if(n<1||n>2)throw std::runtime_error("Ticks per frame must be 1 or 2");o.ticks_per_frame=uint32_t(n);}
        else if(name=="--seconds"){o.seconds=real(val(),"duration");if(o.seconds<=0)throw std::runtime_error("Duration must be positive");}
        else if(name=="--capture-frame")o.capture_frame=integer(val(),"capture frame");
        else if(name=="--width"){auto n=integer(val(),"width");if(n>16384)throw std::runtime_error("Width exceeds 16384");o.width=uint32_t(n);explicit_width=true;}
        else if(name=="--height"){auto n=integer(val(),"height");if(n>16384)throw std::runtime_error("Height exceeds 16384");o.height=uint32_t(n);explicit_height=true;}
        else if(name=="--samples"){auto n=integer(val(),"sample count");if(n!=1&&n!=4)throw std::runtime_error("Samples must be 1 or 4");o.samples=uint32_t(n);explicit_samples=true;}
        else if(name=="--shadow-size"){auto n=integer(val(),"shadow size");if(n!=1024&&n!=2048)throw std::runtime_error("Shadow size must be 1024 or 2048");o.shadow_size=uint32_t(n);explicit_shadow=true;}
        else if(name=="--output")o.output=val();
        else if(name=="--resources")o.resources=val();
        else if(name=="--environment"){auto n=integer(val(),"environment");if(n>1)throw std::runtime_error("Environment must be 0 or 1");o.environment=uint32_t(n);}
        else if(name=="--exposure"){o.exposure=real(val(),"exposure");explicit_exposure=true;}
        else if(name=="--headless"&&eq==std::string_view::npos)o.headless=true;
        else if(name=="--benchmark"&&eq==std::string_view::npos)o.benchmark=true;
        else if(name=="--list-scenes"&&eq==std::string_view::npos)o.list_scenes=true;
        else if(name=="--help"||name=="-h")o.help=true;
        else throw std::runtime_error("Unknown argument: "+std::string(arg));
    }
    const auto*scene=kgfx_demo_catalog::find(o.scene);
    if(!scene)throw std::runtime_error("Unknown demo: "+o.scene);
    if(!kgfx_demo_catalog::contains(scene->views,o.view))throw std::runtime_error("Unknown view for "+o.scene+": "+o.view);
    if(!kgfx_demo_catalog::contains(scene->sequences,o.sequence))throw std::runtime_error("Unknown sequence for "+o.scene+": "+o.sequence);
    if(!scene->walkthrough&&o.diagnostic>4)throw std::runtime_error("Depth/layer diagnostics require a walkthrough scene");
    if(o.preset.empty())o.preset=scene->walkthrough?"cinematic":"quality";
    GraphicsSettings preset;
    if(o.preset=="cinematic")preset=cinematic_settings();
    else if(o.preset=="quality")preset=quality_settings();
    else if(o.preset=="performance")preset=performance_settings();
    else throw std::runtime_error("Unknown preset: "+o.preset);
    // Explicit dimensions/AA/shadows always override the preset, independent of CLI order.
    if(!explicit_width)o.width=preset.width;if(!explicit_height)o.height=preset.height;
    if(!explicit_samples)o.samples=preset.samples;if(!explicit_shadow)o.shadow_size=preset.shadow_size;o.effects=preset.effects;
    // The matched industrial fixtures use physical intensities. Both variants
    // share the same photographic exposure; legacy scenes retain their scale.
    if(!explicit_exposure&&(o.scene=="lighting_baked"||o.scene=="lighting_dynamic"))o.exposure=.05f;
    if(!o.width||!o.height||o.exposure<=0)throw std::runtime_error("Dimensions and exposure must be positive");
    if(o.seconds&&o.frames)throw std::runtime_error("Choose --seconds or --frames");
    if(o.ticks_per_frame!=1&&(!o.frames||!scene->walkthrough))throw std::runtime_error("Multiple ticks require a bounded walkthrough scene");
    if(o.seconds<0||o.seconds>600)throw std::runtime_error("Duration must be within 0..600 seconds");
    if(o.capture_frame&&o.output.empty())throw std::runtime_error("--capture-frame requires --output");
    if(!o.output.empty()){if(o.output.extension()!=".ppm")throw std::runtime_error("Captures use the .ppm format");if(!o.capture_frame&&!o.seconds)o.capture_frame=o.frames?o.frames:1;if(o.frames&&o.capture_frame>o.frames)throw std::runtime_error("Capture frame exceeds frame limit");}
    if(o.headless&&!o.frames)throw std::runtime_error("A headless run requires --frames");return o;
}
int run(kgfx_device*device,const Options&options,const Host&host){
    try{
        auto active=std::make_unique<Renderer>(device,options);
        TimingSamples timings,total_timings,simulation_timings,begin_timings,submission_timings,gpu_timings;
        GpuFrameTimers gpu_timers(device);
        bool captured=false;kgfx_completion last=0;
        uint64_t executed=0,steady_frame_allocations=0,steady_frames=0,warmup=0,scene_switches=0;
        uint64_t captured_frame=0,captured_tick=0;
        uint32_t captured_width=0,captured_height=0,captured_environment=0,captured_diagnostic=0,captured_samples=0,captured_shadow_size=0,captured_effects=0;
        float captured_exposure=1;std::string captured_scene,captured_view,captured_sequence;ShowcaseState captured_state{};
        ShowcaseTimings captured_passes{};bool captured_showcase=false;
        std::cout<<"kstar_gfx "<<kgfx_backend_name()<<" / "<<kgfx_device_caps(device)->renderer<<" / "<<options.scene<<"\n";
        const auto began=Clock::now();auto previous=began;
        while(!options.frames||executed<options.frames){
            Input input;const bool requested_quit=host.pump&&!host.pump(host.user,input);
            const auto now=Clock::now();const double dt=std::chrono::duration<double>(now-previous).count();previous=now;
            if(!requested_quit)active->input(input,dt);
            const auto simulation_end=Clock::now();
            bool switched=false;
            if(!active->requested_scene.empty()){
                const bool matched=active->match_requested;active->match_requested=false;
                auto next=active->options;next.scene=active->requested_scene;active->requested_scene.clear();
                next.view="overview";next.sequence="idle";next.width=active->width;next.height=active->height;
                next.samples=active->settings.samples;next.shadow_size=active->settings.shadow_size;next.effects=active->settings.effects;
                next.environment=active->environment_index;next.exposure=active->exposure;next.diagnostic=0;
                if(matched){next.view=active->options.view;next.sequence=active->options.sequence;next.diagnostic=active->diagnostic;}
                try{
                    auto candidate=std::make_unique<Renderer>(device,next);
                    if(matched&&candidate->world&&active->world){candidate->world->match_view(*active->world);candidate->frame=active->frame;}
                    if(last)active->r.check(kgfx_wait(device,last,30'000'000'000ull),"wait before scene replacement");
                    candidate->presentation_width=active->presentation_width;candidate->presentation_height=active->presentation_height;
                    candidate->follow_window=active->follow_window;active.swap(candidate);switched=true;warmup=0;++scene_switches;
                    previous=Clock::now();std::cout<<"GFX_SCENE active="<<active->options.scene<<'\n';
                }catch(const std::exception&e){
                    active->menu.open(active->settings,active->environment_index,active->exposure,active->diagnostic);
                    active->menu.pending_scene=active->scene_index();active->menu.status="SCENE LOAD FAILED - CURRENT SCENE KEPT.";active->overlay_dirty=true;
                    std::cerr<<"GFX_SCENE rejected: "<<e.what()<<'\n';previous=Clock::now();
                }
            }
            auto&renderer=*active;
            if(host.capture_pointer)host.capture_pointer(host.user,renderer.world&&!renderer.world->state().inspection&&!renderer.menu.visible&&input.focused);
            const bool finished=requested_quit||renderer.quit_requested||(options.seconds>0&&std::chrono::duration<double>(Clock::now()-began).count()>=options.seconds);
            const bool capture_on_exit=finished&&!captured&&!options.output.empty()&&!options.capture_frame;
            if(finished&&!capture_on_exit)break;
            kgfx_stats before{};kgfx_get_stats(device,&before);const auto start=Clock::now();kgfx_readback*readback=nullptr;
            const bool capture=capture_on_exit||options.capture_frame==executed+1;
            const bool sample=(!options.benchmark||warmup>=60)&&!capture&&!switched;
            gpu_timers.poll(gpu_timings);
            last=renderer.render(capture,&readback,gpu_timers,sample);const auto end=Clock::now();
            if(!capture&&!switched&&renderer.options.scene!="stress"){
                steady_frame_allocations+=renderer.stats.steady_state_allocations-before.steady_state_allocations;++steady_frames;
            }
            if(sample){timings.add(elapsed_ms(start,end));total_timings.add(elapsed_ms(now,end));simulation_timings.add(elapsed_ms(now,simulation_end));begin_timings.add(renderer.frame_begin_ms);submission_timings.add(renderer.submission_ms);}++executed;++warmup;
            if(capture){
                renderer.capture(readback,last);captured=true;captured_frame=executed;captured_width=renderer.width;captured_height=renderer.height;
                captured_environment=renderer.environment_index;captured_diagnostic=renderer.diagnostic;captured_exposure=renderer.exposure;
                captured_samples=renderer.options.scene=="triangle"?1:renderer.settings.samples;captured_shadow_size=renderer.options.scene=="triangle"?0:renderer.settings.shadow_size;
                captured_effects=renderer.settings.effects;captured_scene=renderer.options.scene;captured_view=renderer.options.view;captured_sequence=renderer.options.sequence;
                captured_tick=renderer.world?renderer.world->state().tick:renderer.frame;if(renderer.world)captured_state=renderer.world->state();
                captured_showcase=bool(renderer.showcase);if(renderer.showcase)captured_passes=renderer.showcase->timings();
            }
            if(host.title&&executed%30==0){char title[256];std::snprintf(title,sizeof(title),"kstar_gfx | %s | %ux%u | %.2f ms CPU | PageUp/Down scenes | Esc settings",renderer.options.scene.c_str(),renderer.width,renderer.height,elapsed_ms(start,end));host.title(host.user,title);}
            if(finished)break;
        }
        if(host.capture_pointer)host.capture_pointer(host.user,false);
        if(last)active->r.check(kgfx_wait(device,last,30'000'000'000ull),"wait for final frame");
        gpu_timers.poll(gpu_timings);
        if(!options.output.empty()&&!captured)throw std::runtime_error("Run ended before the requested capture frame");
        const auto frame_summary=timings.summarize(),begin_summary=begin_timings.summarize(),submission_summary=submission_timings.summarize(),gpu_summary=gpu_timings.summarize();
        const auto total_summary=total_timings.summarize(),simulation_summary=simulation_timings.summarize();
        const double mean=frame_summary.mean,p50=frame_summary.p50,p95=frame_summary.p95;
        const auto&stats=active->stats;
        std::cout<<"frames="<<executed<<" CPU frame mean="<<mean<<"ms p50="<<p50<<"ms p95="<<p95<<"ms render_allocations="<<steady_frame_allocations<<" over "<<steady_frames<<" non-capture frames; total resource allocations="<<stats.steady_state_allocations<<'\n';
        std::cout<<"CPU total frame p95="<<total_summary.p95<<"ms; CPU simulation/input p95="<<simulation_summary.p95<<"ms; bounded ticks/frame="<<options.ticks_per_frame<<'\n';
        std::cout<<"CPU frame begin (slot wait/poll) p95="<<begin_summary.p95<<"ms; CPU recording/submission p95="<<submission_summary.p95<<"ms; GPU timestamps="<<(gpu_timers.supported?"supported":"unsupported");
        if(gpu_timers.supported)std::cout<<" mean="<<gpu_summary.mean<<"ms p95="<<gpu_summary.p95<<"ms samples="<<gpu_summary.samples<<" skipped="<<gpu_timers.skipped;
        std::cout<<'\n';
        if(!options.output.empty()){
            auto json_path=options.output;json_path.replace_extension(".json");std::ofstream j(json_path);if(!j)throw std::runtime_error("Cannot create capture metadata");
            j<<std::setprecision(9)<<"{\n  \"backend\": \""<<json_escape(kgfx_backend_name())<<"\",\n  \"renderer\": \""<<json_escape(kgfx_device_caps(device)->renderer)<<"\",\n  \"demo\": \""<<captured_scene<<"\",\n  \"view\": \""<<captured_view<<"\",\n  \"sequence\": \""<<captured_sequence<<"\",\n  \"width\": "<<captured_width<<", \"height\": "<<captured_height<<",\n  \"frames\": "<<executed<<", \"capture_frame\": "<<captured_frame<<",\n  \"scene_tick\": "<<captured_tick<<", \"scene_switches\": "<<scene_switches<<",\n  \"presentation_enabled\": "<<(options.headless?"false":"true")<<",\n  \"fixed_step_seconds\": 0.016666666666666666, \"ticks_per_frame\": "<<options.ticks_per_frame<<", \"seed\": "<<options.seed<<",\n  \"environment\": "<<captured_environment<<", \"diagnostic\": "<<captured_diagnostic<<", \"exposure\": "<<captured_exposure<<",\n  \"quality\": {\"msaa\": "<<captured_samples<<", \"shadow_size\": "<<captured_shadow_size<<", \"effects\": "<<captured_effects<<", \"tone_map\": \"ACES fitted\"},\n  \"interactions\": {\"glass_variant\": "<<captured_state.glass_variant<<", \"lights_on\": "<<(captured_state.lights_on?"true":"false")<<", \"water_on\": "<<(captured_state.water_on?"true":"false")<<", \"sprinklers_on\": "<<(captured_state.sprinklers_on?"true":"false")<<", \"fire_level\": "<<captured_state.fire_level<<", \"particles\": "<<captured_state.particle_count<<", \"animation_frozen\": "<<(captured_state.animation_frozen?"true":"false")<<", \"animation_tick\": "<<captured_state.animation_tick<<", \"light_color_variant\": "<<captured_state.light_color_variant<<", \"stationary_intensity\": "<<captured_state.stationary_intensity<<"},\n  \"cpu_frame_ms\": {\"mean\": "<<mean<<", \"p50\": "<<p50<<", \"p95\": "<<p95<<", \"samples\": "<<frame_summary.samples<<", \"total_samples\": "<<frame_summary.total<<"},\n  \"cpu_total_frame_ms\": {\"mean\": "<<total_summary.mean<<", \"p50\": "<<total_summary.p50<<", \"p95\": "<<total_summary.p95<<", \"samples\": "<<total_summary.samples<<"},\n  \"cpu_simulation_ms\": {\"mean\": "<<simulation_summary.mean<<", \"p50\": "<<simulation_summary.p50<<", \"p95\": "<<simulation_summary.p95<<", \"samples\": "<<simulation_summary.samples<<"},\n  \"cpu_frame_begin_ms\": {\"mean\": "<<begin_summary.mean<<", \"p50\": "<<begin_summary.p50<<", \"p95\": "<<begin_summary.p95<<", \"samples\": "<<begin_summary.samples<<"},\n  \"cpu_submission_ms\": {\"mean\": "<<submission_summary.mean<<", \"p50\": "<<submission_summary.p50<<", \"p95\": "<<submission_summary.p95<<", \"samples\": "<<submission_summary.samples<<"},\n  \"gpu_frame_ms\": {\"status\": \""<<(gpu_timers.supported?"supported":"unsupported")<<"\", \"mean\": ";
            if(gpu_timers.supported&&gpu_summary.samples)j<<gpu_summary.mean;else j<<"null";
            j<<", \"p50\": ";if(gpu_timers.supported&&gpu_summary.samples)j<<gpu_summary.p50;else j<<"null";
            j<<", \"p95\": ";if(gpu_timers.supported&&gpu_summary.samples)j<<gpu_summary.p95;else j<<"null";
            j<<", \"samples\": "<<gpu_summary.samples<<", \"skipped\": "<<gpu_timers.skipped<<"},\n  \"pass_timings\": [";
            if(captured_showcase)for(size_t pass=0;pass<ShowcaseTimings::count;++pass){
                if(pass)j<<",";
                j<<"\n    {\"name\": \""<<ShowcaseTimings::names[pass]<<"\", \"cpu_ms\": "<<captured_passes.cpu_ms[pass]<<", \"gpu_ms\": ";
                if(captured_passes.gpu_valid[pass])j<<captured_passes.gpu_ms[pass];else j<<"null";
                j<<", \"gpu_frame_serial\": "<<captured_passes.gpu_frame_serial[pass]<<"}";
            }
            j<<"\n  ],\n  \"stats\": {\"draws\": "<<stats.draws<<", \"instances\": "<<stats.instances<<", \"upload_bytes\": "<<stats.upload_bytes<<", \"steady_state_allocations\": "<<stats.steady_state_allocations<<", \"steady_frame_allocations\": "<<steady_frame_allocations<<", \"steady_frames\": "<<steady_frames<<"},\n  \"capture_source\": \"completed GPU LDR render target\"\n}\n";
            if(!j)throw std::runtime_error("Cannot write capture metadata");
        }
        return 0;
    }catch(const std::exception&e){if(host.capture_pointer)host.capture_pointer(host.user,false);std::cerr<<"kstar_gfx_demo: "<<e.what()<<'\n';return 1;}
}
}
