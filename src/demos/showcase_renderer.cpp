#include "showcase_renderer.hpp"
#include "assets.hpp"
#include "shadow_cache.hpp"
#include "water_surface.hpp"
#include <bit>
#include "showcase.vert.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <chrono>
#include <cstring>
#include <limits>
#include <numeric>
#include <stdexcept>
#include <string>
#include <vector>

namespace demo {
namespace {
constexpr size_t max_objects=2048,max_draws=4096,max_globals=80;
struct alignas(16) Globals {
    M4 view_projection,inverse_view_projection,directional_projection,reflection_projection;
    M4 spot_projection[32];
    M4 directional_far;
    V4 cascade,probe_origin,probe_spacing,probe_counts,sky_color;
    M4 volume_inverse[4];V4 volume_params[4],volume_color[4],volume_info;
    V4 camera_time,sun_direction,sun_color,viewport,params,fog,clip_plane,camera_right,camera_up,post;
};
struct alignas(16) ObjectUniforms {
    M4 model,normal_matrix;
    V4 base_color,material,emissive,surface,tint,object_options;
    V4 light_position[8],light_color[8],light_direction[8],light_shadow[2];
    V4 light_right[8],light_shape[8],light_control[8],light_diffuse[2];
    V4 baked_direct[6],baked_indirect[6],mask_uv,mask_params;
    M4 decal_projection[8];
    V4 decal_color[8],decal_params[8];
    V4 lightmap_u[6],lightmap_v[6],lightmap_options;
};
KGFX_SHOWCASE_VERT_SPV_KGFX_U0_ASSERT_LAYOUT(Globals);
KGFX_SHOWCASE_VERT_SPV_KGFX_U1_ASSERT_LAYOUT(ObjectUniforms);
struct ParticleInstance {V4 position_size,color,velocity_kind,phase_rotation,illumination;};
struct TextureSet {kgfx_texture*albedo=nullptr,*normal=nullptr,*orm=nullptr;};
struct RenderDraw {const Mesh*mesh;uint32_t first,count;size_t object;bool transparent,two_sided,moving;V3 center;float radius;TextureSet maps;};
struct Targets {
    Resources r;
    kgfx_texture *hdr,*depth,*snapshot,*resolved,*ldr,*linear_depth,*linear_z,*ao[2],*peel[3],*peel_z;
    kgfx_texture *sun,*spots,*reflection,*reflection_z,*bloom[2],*shafts,*volume;
    uint32_t rw,rh,bw,bh,aw,ah,local_tile,vw,vh,sw,sh;
    Targets(kgfx_device*d,const GraphicsSettings&s):r(d){
        auto target=[&](uint32_t w,uint32_t h,kgfx_format f,uint32_t samples=1){return r.texture(w,h,f,1,samples,KGFX_TEXTURE_TARGET|(samples==1?KGFX_TEXTURE_SAMPLED:0));};
        hdr=target(s.width,s.height,KGFX_RGBA16F,s.samples);depth=target(s.width,s.height,KGFX_DEPTH32F,s.samples);
        snapshot=target(s.width,s.height,KGFX_RGBA16F);resolved=target(s.width,s.height,KGFX_RGBA16F);ldr=target(s.width,s.height,KGFX_RGBA8);
        linear_depth=target(s.width,s.height,KGFX_R32F);linear_z=target(s.width,s.height,KGFX_DEPTH32F);
        aw=std::max(1u,s.width/2);ah=std::max(1u,s.height/2);
        for(auto&x:ao)x=target(aw,ah,KGFX_RGBA8);
        sw=std::max(1u,s.width/(s.effects==1?8:4));sh=std::max(1u,s.height/(s.effects==1?8:4));shafts=target(sw,sh,KGFX_RGBA16F);vw=std::max(1u,s.width/(s.effects==1?4:2));vh=std::max(1u,s.height/(s.effects==1?4:2));volume=target(vw,vh,KGFX_RGBA16F);
        for(auto&x:peel)x=target(s.width,s.height,KGFX_R32F);
        peel_z=target(s.width,s.height,KGFX_DEPTH32F);
        sun=target(s.shadow_size*2,s.shadow_size*2,KGFX_DEPTH32F);
        local_tile=s.effects==1?256:512;spots=target(local_tile*8,local_tile*8,KGFX_DEPTH32F);
        const uint32_t divisor=s.effects==1?4:2;
        rw=std::max(1u,s.width/divisor);rh=std::max(1u,s.height/divisor);
        reflection=target(rw,rh,KGFX_RGBA16F);reflection_z=target(rw,rh,KGFX_DEPTH32F);
        bw=std::max(1u,s.width/4);bh=std::max(1u,s.height/4);for(auto&x:bloom)x=target(bw,bh,KGFX_RGBA16F);
    }
};
struct PassMeasurements {
    using Clock=std::chrono::steady_clock;
    kgfx_device*device;
    ShowcaseTimings values{};
    std::array<std::array<kgfx_gpu_timer*,4>,ShowcaseTimings::count> timers{};
    std::array<std::array<bool,4>,ShowcaseTimings::count> pending{};
    std::array<bool,ShowcaseTimings::count> recording{};
    std::array<Clock::time_point,ShowcaseTimings::count> started{};
    uint64_t frame=0;size_t slot=0;
    explicit PassMeasurements(kgfx_device*d):device(d){
        for(auto&ring:timers)for(auto&timer:ring){
            const auto result=kgfx_gpu_timer_create(d,&timer);
            if(result==KGFX_UNSUPPORTED)return;
            if(result!=KGFX_OK){release();throw std::runtime_error("Cannot create showcase GPU timer");}
        }
        values.gpu_supported=true;
    }
    void release(){for(auto&ring:timers)for(auto*&timer:ring){kgfx_gpu_timer_destroy(device,timer);timer=nullptr;}}
    ~PassMeasurements(){release();}
    void next_frame(){
        values.cpu_ms.fill(0);slot=size_t(frame++%4);
        if(!values.gpu_supported)return;
        for(size_t pass=0;pass<timers.size();++pass)for(size_t i=0;i<4;++i)if(pending[pass][i]){
            kgfx_gpu_timer_result result{};
            if(kgfx_gpu_timer_read(device,timers[pass][i],&result)==KGFX_OK){
                if(result.frame_serial>=values.gpu_frame_serial[pass]){
                    values.gpu_ms[pass]=float(double(result.elapsed_ns)*1e-6);
                    values.gpu_frame_serial[pass]=result.frame_serial;values.gpu_valid[pass]=true;
                }
                pending[pass][i]=false;
            }
        }
    }
    void begin(size_t pass){
        started[pass]=Clock::now();recording[pass]=false;
        if(values.gpu_supported&&!pending[pass][slot])
            recording[pass]=kgfx_gpu_timer_begin(device,timers[pass][slot])==KGFX_OK;
    }
    void end(size_t pass){
        if(recording[pass]){kgfx_gpu_timer_end(device,timers[pass][slot]);pending[pass][slot]=true;recording[pass]=false;}
        values.cpu_ms[pass]+=std::chrono::duration<float,std::milli>(Clock::now()-started[pass]).count();
    }
};
size_t aligned(size_t n,size_t alignment){return (n+alignment-1)/alignment*alignment;}
Mesh cylinder(Resources&r){
    std::vector<Vertex>vertices;std::vector<uint32_t>indices;constexpr unsigned slices=32;
    for(unsigned cap=0;cap<3;++cap){
        const uint32_t base=uint32_t(vertices.size());
        if(cap==0){
            for(unsigned y=0;y<2;++y)for(unsigned i=0;i<=slices;++i){float a=float(i)*2*pi/float(slices),x=std::cos(a),z=std::sin(a);vertices.push_back({{x*.5f,float(y)-.5f,z*.5f},{x,0,z},{-z,0,x,1},{float(i)/float(slices),float(y)}});}
            for(unsigned i=0;i<slices;++i){uint32_t a=base+i,b=a+slices+1;indices.insert(indices.end(),{a,b,a+1,a+1,b,b+1});}
        }else{
            float y=cap==1?-.5f:.5f,normal=cap==1?-1.0f:1.0f;vertices.push_back({{0,y,0},{0,normal,0},{1,0,0,1},{.5f,.5f}});
            for(unsigned i=0;i<=slices;++i){float a=float(i)*2*pi/float(slices),x=std::cos(a),z=std::sin(a);vertices.push_back({{x*.5f,y,z*.5f},{0,normal,0},{1,0,0,1},{x*.5f+.5f,z*.5f+.5f}});}
            for(unsigned i=0;i<slices;++i)if(cap==1)indices.insert(indices.end(),{base,base+i+1,base+i+2});else indices.insert(indices.end(),{base,base+i+2,base+i+1});
        }
    }
    Mesh m;m.vertices=r.buffer(vertices.size()*sizeof(Vertex),KGFX_BUFFER_VERTEX,vertices.data());m.indices=r.buffer(indices.size()*4,KGFX_BUFFER_INDEX,indices.data());m.vertex_count=uint32_t(vertices.size());m.index_count=uint32_t(indices.size());m.lo={-.5f,-.5f,-.5f};m.hi={.5f,.5f,.5f};return m;
}
}

struct ShowcaseRenderer::Impl {
    kgfx_device*device;Resources r;std::filesystem::path root;GraphicsSettings settings;
    PassMeasurements measurements;
    std::unique_ptr<Targets>targets;
    std::array<Mesh,7>meshes;
    std::array<M4,7>mesh_adjust;
    std::array<TextureSet,4>textures;
    std::array<Environment,2>environments;
    kgfx_texture *white,*normal,*orm,*grime,*foam,*water_normal,*fire,*smoke,*spray,*sparks,*lookup,*probe_texture,*surface_masks,*water_state,*lightmap;
    std::array<Mesh,max_water_streams>stream_meshes;WaterStreamGeometry stream_geometry;
    std::array<std::array<Vertex,WaterStreamGeometry::vertex_count>,max_water_streams>stream_vertices{};size_t stream_count=0;
    std::array<Mesh,16>crown_meshes;SplashCrownGeometry crown_geometry;
    std::array<std::array<Vertex,SplashCrownGeometry::vertex_count>,16>crown_vertices{};size_t crown_count=0;
    Mesh water_mesh;std::array<Vertex,65*65> water_vertices{};
    std::array<V4,65*65> water_pixels{};std::array<V4,18*192> probe_pixels{};
    std::array<kgfx_texture*,3>volume_atlases{},volume_distant{};
    kgfx_sampler *repeat,*clamp,*nearest,*mip_clamp;
    kgfx_buffer *uniforms,*particle_buffer,*depth_instances;
    std::vector<M4>depth_transforms;
    struct DepthBatch {const Mesh*mesh=nullptr;size_t offset=0;uint32_t count=0;bool two_sided=false;};
    std::array<std::array<DepthBatch,14>,max_globals>depth_batches{};
    std::array<unsigned,max_globals>depth_batch_counts{};
    size_t global_stride=0,object_stride=0,object_base=0,global_count=0;
    std::vector<uint8_t>uniform_bytes;
    std::vector<RenderDraw>draws;
    std::vector<ParticleInstance>particle_data;
    std::vector<uint32_t>particle_order;
    std::vector<size_t>light_order;
    std::array<size_t,128>decal_order{};
    ShadowCache shadow_cache;
    std::array<int,32>shadow_lights{};std::array<bool,32>shadow_dirty{};
    bool shadow_initialized=false;size_t shadow_count=0;
    bool sun_initialized=false;std::array<uint64_t,2>sun_keys{};std::array<bool,2>sun_dirty{};
    bool moving_shadows=false,previous_moving_shadows=false;
    kgfx_pipeline *surface_pipeline[2][2]{},*reflected_pipeline[2]{},*depth_pipeline[2]{},*peel_pipeline[2]{},*shadow_pipeline[2]{};
    kgfx_pipeline *depth_instanced_pipeline[2]{},*shadow_instanced_pipeline[2]{};
    kgfx_pipeline *sky_pipeline[2]{},*particle_pipeline[2]{},*ao_pipeline,*blur_pipeline,*bloom_pipeline,*tone_pipeline,*shafts_pipeline,*shadow_clear_pipeline,*volume_pipeline,*volume_composite[2];
    struct CachedTransform {M4 authored{},model{},normal{};ShowcaseGeometry geometry{};bool valid=false;};
    std::array<CachedTransform,max_objects> transform_cache{};
    Globals base{};
    uint32_t environment_index=0;
    size_t particle_uniform_index=0;
    Impl(kgfx_device*d,const std::filesystem::path&path,const GraphicsSettings&s):device(d),r(d),root(path),settings(s),measurements(d){
        const auto*caps=kgfx_device_caps(d);if(!caps||caps->max_texture_bindings<16||caps->max_samples<4)throw std::runtime_error("Showcase requires 16 texture bindings and 4x MSAA");
        const size_t alignment=std::max(16u,caps->uniform_alignment);global_stride=aligned(sizeof(Globals),alignment);object_stride=aligned(sizeof(ObjectUniforms),alignment);object_base=global_stride*max_globals;
        uniform_bytes.resize(object_base+object_stride*(max_draws+1));uniforms=r.buffer(uniform_bytes.size(),KGFX_BUFFER_UNIFORM|KGFX_BUFFER_DYNAMIC);
        particle_buffer=r.buffer(ShowcaseWorld::max_particles*sizeof(ParticleInstance),KGFX_BUFFER_VERTEX|KGFX_BUFFER_DYNAMIC);
        depth_instances=r.buffer(max_objects*max_globals*sizeof(M4),KGFX_BUFFER_VERTEX|KGFX_BUFFER_DYNAMIC);
        depth_transforms.reserve(max_objects*max_globals);
        draws.reserve(max_draws);particle_data.reserve(ShowcaseWorld::max_particles);particle_order.reserve(ShowcaseWorld::max_particles);light_order.reserve(128);
        repeat=r.sampler();clamp=r.sampler(false,KGFX_CLAMP);nearest=r.sampler(false,KGFX_CLAMP,KGFX_NEAREST);mip_clamp=r.sampler(true,KGFX_CLAMP);
        const uint8_t white_pixel[]{255,255,255,255},normal_pixel[]{128,128,255,255};white=solid(white_pixel);normal=solid(normal_pixel);orm=white;
        const auto cooked=root/"assets/cooked";
        const std::array names{"concrete_floor_painted","rusty_metal_02","rock_tile_floor"};
        for(size_t i=0;i<names.size();++i){auto path_for=[&](const char*map){return cooked/"textures"/(std::string(names[i])+"_"+map+"_2k.kgtex");};textures[i]={r.load_texture(path_for("diff")),r.load_texture(path_for("nor_gl")),r.load_texture(path_for("arm"))};}
        textures[3]={white,normal,orm};
        auto effect=[&](const char*name){return r.load_texture(cooked/"effects"/(std::string(name)+".kgtex"));};
        fire=effect("fire");smoke=effect("smoke");spray=effect("water_spray");sparks=effect("sparks");water_normal=effect("water_normal");foam=effect("foam");grime=effect("grime");surface_masks=effect("surface_masks");
        lookup=r.load_texture(cooked/"lighting/lookup.kgtex");lightmap=r.load_texture(cooked/"lighting/industrial_lightmap.kgtex");
        const std::array volume_names{"small_campfire","simple_fire","chimney_smoke"};for(unsigned i=0;i<3;++i){volume_atlases[i]=r.load_texture(cooked/"effects/volumes"/volume_names[i]/"volume.kgtex");volume_distant[i]=r.load_texture(cooked/"effects/volumes"/volume_names[i]/"distant.kgtex");}
        probe_texture=r.texture(18,192,KGFX_RGBA32F);water_state=r.texture(65,65,KGFX_RGBA32F);
        for(auto&mesh:crown_meshes){mesh.vertices=r.buffer(SplashCrownGeometry::vertex_count*sizeof(Vertex),KGFX_BUFFER_VERTEX|KGFX_BUFFER_DYNAMIC);mesh.indices=r.buffer(sizeof(crown_geometry.indices),KGFX_BUFFER_INDEX,crown_geometry.indices.data());mesh.vertex_count=SplashCrownGeometry::vertex_count;mesh.index_count=SplashCrownGeometry::index_count;}
        water_mesh.vertices=r.buffer(water_vertices.size()*sizeof(Vertex),KGFX_BUFFER_VERTEX|KGFX_BUFFER_DYNAMIC);
        std::vector<uint32_t>water_indices;water_indices.reserve(64*64*6);
        for(uint32_t z=0;z<64;++z)for(uint32_t x=0;x<64;++x){uint32_t a=z*65+x;water_indices.insert(water_indices.end(),{a,a+65,a+1,a+1,a+65,a+66});}
        for(auto&mesh:stream_meshes){mesh.vertices=r.buffer(WaterStreamGeometry::vertex_count*sizeof(Vertex),KGFX_BUFFER_VERTEX|KGFX_BUFFER_DYNAMIC);mesh.indices=r.buffer(sizeof(stream_geometry.indices),KGFX_BUFFER_INDEX,stream_geometry.indices.data());mesh.vertex_count=WaterStreamGeometry::vertex_count;mesh.index_count=WaterStreamGeometry::index_count;}
        water_mesh.indices=r.buffer(water_indices.size()*sizeof(uint32_t),KGFX_BUFFER_INDEX,water_indices.data());water_mesh.vertex_count=65*65;water_mesh.index_count=uint32_t(water_indices.size());
        environments[0]=r.load_environment(cooked/"industrial_pipe_and_valve_01.kgenv");environments[1]=r.load_environment(cooked/"qwantani_night.kgenv");
        meshes[0]=make_cube(r);meshes[1]=make_plane(r);meshes[2]=make_sphere(r,24,16);meshes[3]=cylinder(r);
        meshes[4]=r.load_model(cooked/"modular_industrial_pipes_01_pipe01.kgmodel");meshes[5]=r.load_model(cooked/"power_box_01.kgmodel");meshes[6]=r.load_model(cooked/"Barrel_01.kgmodel");
        for(size_t i=0;i<meshes.size();++i){mesh_adjust[i]=i==2?scale({.5f,.5f,.5f}):identity();if(i>=4){const auto&m=meshes[i];V3 span=m.hi-m.lo,mid=(m.hi+m.lo)*.5f;mesh_adjust[i]=scale({1/std::max(span.x,.001f),1/std::max(span.y,.001f),1/std::max(span.z,.001f)})*translate(mid*-1);}}
        for(unsigned two=0;two<2;++two){
            const auto cull=two?KGFX_CULL_NONE:KGFX_CULL_BACK;
            depth_pipeline[two]=pipeline("showcase.vert","showcase_depth.frag",KGFX_R32F,1,true,true,cull);
            depth_instanced_pipeline[two]=pipeline("showcase_depth_instanced.vert","showcase_depth.frag",KGFX_R32F,1,true,true,cull);
            peel_pipeline[two]=pipeline("showcase.vert","showcase_peel.frag",KGFX_R32F,1,true,true,cull);
            shadow_pipeline[two]=pipeline("showcase.vert","showcase_shadow.frag",KGFX_RGBA8,1,true,true,cull);
            shadow_instanced_pipeline[two]=pipeline("showcase_depth_instanced.vert","showcase_shadow.frag",KGFX_RGBA8,1,true,true,cull);
            reflected_pipeline[two]=pipeline("showcase.vert","showcase_opaque.frag",KGFX_RGBA16F,1,true,true,two?KGFX_CULL_NONE:KGFX_CULL_FRONT);
            for(unsigned aa=0;aa<2;++aa)surface_pipeline[aa][two]=pipeline("showcase.vert","showcase_opaque.frag",KGFX_RGBA16F,aa?4:1,true,true,cull);
        }
        for(unsigned aa=0;aa<2;++aa){sky_pipeline[aa]=pipeline("fullscreen.vert","showcase_sky.frag",KGFX_RGBA16F,aa?4:1,false,false,KGFX_CULL_NONE);particle_pipeline[aa]=pipeline("showcase_particle.vert","showcase_particle.frag",KGFX_RGBA16F,aa?4:1,true,false,KGFX_CULL_NONE,KGFX_BLEND_PREMULTIPLIED,true);}
        // Refractors do not change opaque depth; each peeled layer replaces the
        // covered color sample with its already-composited transmission result.
        for(unsigned aa=0;aa<2;++aa)for(unsigned two=0;two<2;++two)transparent_pipeline[aa][two]=pipeline("showcase.vert","showcase_surface.frag",KGFX_RGBA16F,aa?4:1,true,false,two?KGFX_CULL_NONE:KGFX_CULL_BACK);
        ao_pipeline=pipeline("fullscreen.vert","showcase_ssao.frag",KGFX_RGBA8,1,false,false,KGFX_CULL_NONE);
        blur_pipeline=pipeline("fullscreen.vert","showcase_blur.frag",KGFX_RGBA8,1,false,false,KGFX_CULL_NONE);
        bloom_pipeline=pipeline("fullscreen.vert","showcase_bloom.frag",KGFX_RGBA16F,1,false,false,KGFX_CULL_NONE);
        tone_pipeline=pipeline("fullscreen.vert","showcase_tonemap.frag",KGFX_RGBA8,1,false,false,KGFX_CULL_NONE);
        volume_pipeline=pipeline("fullscreen.vert","showcase_volume.frag",KGFX_RGBA16F,1,false,false,KGFX_CULL_NONE);
        for(unsigned aa=0;aa<2;++aa)volume_composite[aa]=pipeline("fullscreen.vert","showcase_volume_composite.frag",KGFX_RGBA16F,aa?4:1,false,false,KGFX_CULL_NONE,KGFX_BLEND_PREMULTIPLIED);
        shafts_pipeline=pipeline("fullscreen.vert","showcase_shafts.frag",KGFX_RGBA16F,1,false,false,KGFX_CULL_NONE);
        shadow_clear_pipeline=pipeline("fullscreen.vert","showcase_clear_shadow.frag",KGFX_RGBA8,1,true,true,KGFX_CULL_NONE,KGFX_BLEND_OPAQUE,false,true);
        targets=std::make_unique<Targets>(device,s);
    }
    kgfx_pipeline*transparent_pipeline[2][2]{};
    kgfx_texture*solid(const uint8_t*pixel){auto*t=r.texture(1,1,KGFX_RGBA8);r.check(kgfx_texture_upload(device,t,0,0,pixel,4),"upload showcase default texture");return t;}
    kgfx_pipeline*pipeline(const char*vs,const char*fs,kgfx_format color,uint32_t samples,bool depth_test,bool depth_write,kgfx_cull cull,kgfx_blend blend=KGFX_BLEND_OPAQUE,bool particles=false,bool clear_shadow=false){
#if defined(KSTAR_GFX_BACKEND_GNM)
        constexpr const char*suffix=".sb";
#else
        constexpr const char*suffix=".glsl";
#endif
        auto v=read_file(root/"shaders"/(std::string(vs)+suffix)),f=read_file(root/"shaders"/(std::string(fs)+suffix));
        kgfx_pipeline_desc p{};p.vertex={v.data(),v.size()};p.fragment={f.data(),f.size()};p.color_format=color;p.depth_format=KGFX_DEPTH32F;p.samples=samples;p.depth_test=depth_test;p.depth_write=depth_write;p.depth_compare=clear_shadow?KGFX_COMPARE_ALWAYS:KGFX_COMPARE_LEQUAL;p.cull=cull;p.blend=blend;
        if(particles){p.attribute_count=5;p.vertex_stride[1]=sizeof(ParticleInstance);for(unsigned i=0;i<4;++i)p.attributes[i]={i+4,i*16,1,1,KGFX_FLOAT4};p.attributes[4]={0,64,1,1,KGFX_FLOAT4};}
        else if(std::strcmp(vs,"showcase_depth_instanced.vert")==0){p.attribute_count=5;p.vertex_stride[0]=sizeof(Vertex);p.vertex_stride[1]=sizeof(M4);p.attributes[0]={0,0,0,0,KGFX_FLOAT3};for(unsigned i=0;i<4;++i)p.attributes[i+1]={i+4,i*16,1,1,KGFX_FLOAT4};}
        else if(depth_test&&!clear_shadow){p.attribute_count=4;p.vertex_stride[0]=sizeof(Vertex);p.attributes[0]={0,0,0,0,KGFX_FLOAT3};p.attributes[1]={1,12,0,0,KGFX_FLOAT3};p.attributes[2]={2,24,0,0,KGFX_FLOAT4};p.attributes[3]={3,40,0,0,KGFX_FLOAT2};}
        kgfx_pipeline*result=nullptr;r.check(kgfx_pipeline_create(device,&p,&result),(std::string("create showcase pipeline ")+vs+" / "+fs).c_str());r.pipelines.push_back(result);return result;
    }
    void resize(const GraphicsSettings&s){auto candidate=std::make_unique<Targets>(device,s);targets.swap(candidate);settings=s;shadow_cache.invalidate();shadow_initialized=false;sun_initialized=false;}
    size_t global(const Globals&g){if(global_count>=max_globals)throw std::runtime_error("Showcase global uniform arena full");size_t slot=global_count++;std::memcpy(uniform_bytes.data()+slot*global_stride,&g,sizeof(g));return slot;}
    Globals dimensions(Globals g,uint32_t w,uint32_t h){g.viewport={float(w),float(h),1.0f/float(w),1.0f/float(h)};return g;}
    static uint64_t light_id(const ShowcaseLight&l,size_t index){return l.id?l.id:0x8000000000000000ull+index;}
    void select_lights(ObjectUniforms&u,V3 center,const ShowcaseFrame&frame,bool moving=false){
        // Bounded insertion keeps the strongest eight without sorting all lights.
        std::array<float,8>scores{};std::array<int,8>selected{};selected.fill(-1);
        for(size_t index=0;index<frame.lights.size();++index){
            const auto&l=frame.lights[index];if(l.type==LightType::Directional||l.type==LightType::Sky)continue;
            const float distance2=dot(l.position-center,l.position-center);
            float score=light_intensity_candela(l)/std::max(distance2,.25f);if(distance2>l.radius*l.radius)score*=.01f;
            for(unsigned k=0;k<8;++k)if(selected[k]<0||score>scores[k]){for(unsigned j=7;j>k;--j){scores[j]=scores[j-1];selected[j]=selected[j-1];}scores[k]=score;selected[k]=int(index);break;}
        }
        u.light_shadow[0]=u.light_shadow[1]={-1,-1,-1,-1};
        for(size_t i=0;i<8;++i)if(selected[i]>=0){
            const size_t index=size_t(selected[i]);const auto&l=frame.lights[index];const V3 color=light_color(l),direction=unit(l.direction),right=unit(l.right);
            u.light_position[i]={l.position.x,l.position.y,l.position.z,l.radius};u.light_color[i]={color.x,color.y,color.z,light_intensity_candela(l)};
            const auto type=l.type==LightType::Point&&l.spot_cosine>0?LightType::Spot:l.type;
            u.light_direction[i]={direction.x,direction.y,direction.z,l.spot_cosine};u.light_right[i]={right.x,right.y,right.z,l.width};
            u.light_shape[i]={float(type),l.inner_cone_cosine,l.height,l.units==LightIntensityUnit::Legacy?0.0f:1.0f};
            u.light_control[i]={l.source_radius,l.shadow_bias,l.shadow_softness,l.volumetric};
            // Lightmaps contain diffuse energy only; retain direct specular,
            // including Static highlights on glass, water and metallic receivers.
            const float diffuse=l.mobility==LightMobility::Static&&frame.bake.valid&&!moving&&u.surface.x<.5f?0.0f:1.0f;
            auto&weights=u.light_diffuse[i/4];switch(i%4){case 0:weights.x=diffuse;break;case 1:weights.y=diffuse;break;case 2:weights.z=diffuse;break;default:weights.w=diffuse;break;}
            if(const auto*a=shadow_cache.find(light_id(l,index))){auto&entry=u.light_shadow[i/4];switch(i%4){case 0:entry.x=float(a->first);break;case 1:entry.y=float(a->first);break;case 2:entry.z=float(a->first);break;default:entry.w=float(a->first);break;}}
        }
    }
    static bool visible(const M4&m,V3 center,float radius){
        for(int axis=0;axis<3;++axis)for(float sign:{-1.0f,1.0f}){
            const float a=m.v[3]+sign*m.v[axis],b=m.v[7]+sign*m.v[4+axis],c=m.v[11]+sign*m.v[8+axis],d=m.v[15]+sign*m.v[12+axis];
            if(a*center.x+b*center.y+c*center.z+d < -radius*std::sqrt(a*a+b*b+c*c))return false;
        }return true;
    }
    void prepare_shadows(const ShowcaseFrame&frame){
        uint64_t geometry=1469598103934665603ull;
        auto hash=[](uint64_t&h,const void*data,size_t bytes){auto*p=static_cast<const uint8_t*>(data);for(size_t i=0;i<bytes;++i){h^=p[i];h*=1099511628211ull;}};
        for(const auto&o:frame.objects)if(!o.movable&&o.material.surface==ShowcaseSurface::Opaque){hash(geometry,&o.transform,sizeof(o.transform));hash(geometry,&o.geometry_revision,sizeof(o.geometry_revision));hash(geometry,&o.geometry,sizeof(o.geometry));hash(geometry,&o.double_sided,sizeof(o.double_sided));}
        for(unsigned i=0;i<2;++i){uint64_t key=geometry;const auto&projection=i?base.directional_far:base.directional_projection;hash(key,&projection,sizeof(projection));hash(key,&base.cascade.w,sizeof(base.cascade.w));sun_dirty[i]=!sun_initialized||key!=sun_keys[i];sun_keys[i]=key;}
        std::array<ShadowCache::Request,128>requests{};size_t count=0;
        for(size_t i=0;i<frame.lights.size();++i){const auto&l=frame.lights[i];if(!l.casts_shadow||l.type==LightType::Directional||l.type==LightType::Sky)continue;
            uint64_t key=geometry;hash(key,&l.position,sizeof(l.position));hash(key,&l.direction,sizeof(l.direction));hash(key,&l.right,sizeof(l.right));hash(key,&l.radius,sizeof(l.radius));hash(key,&l.spot_cosine,sizeof(l.spot_cosine));hash(key,&l.type,sizeof(l.type));
            const unsigned faces=l.type==LightType::Rect?5:(l.type==LightType::Spot||l.spot_cosine>0)?1:6;requests[count++]={light_id(l,i),key,faces};
        }
        shadow_cache.update({requests.data(),count});shadow_lights.fill(-1);shadow_dirty.fill(false);shadow_count=0;
        for(size_t i=0;i<frame.lights.size();++i){const auto&l=frame.lights[i];const auto*a=shadow_cache.find(light_id(l,i));if(!a)continue;
            const V3 forward=unit(l.direction),right=unit(l.right-forward*dot(l.right,forward)),up=unit(cross(right,forward));
            const std::array<V3,6>dirs=a->faces==5?std::array<V3,6>{forward,right,right*-1,up,up*-1,forward}:std::array<V3,6>{V3{1,0,0},V3{-1,0,0},V3{0,1,0},V3{0,-1,0},V3{0,0,1},V3{0,0,-1}};
            for(unsigned f=0;f<a->faces;++f){unsigned slot=a->first+f;shadow_lights[slot]=int(i);shadow_dirty[slot]=a->dirty;shadow_count=std::max(shadow_count,size_t(slot+1));
                V3 dir=a->faces==1?forward:dirs[f];V3 face_up=std::abs(dir.y)>.95f?V3{0,0,1}:V3{0,1,0};
                const float fov=a->faces==1?std::min(2*std::acos(std::clamp(l.spot_cosine,.01f,.999f)),pi*.98f):pi*.5f;
                base.spot_projection[slot]=perspective(fov,1,.04f,std::max(l.radius,.1f))*look_at(l.position,l.position+dir,face_up);
            }
        }
    }
    void prepare(const ShowcaseFrame&frame,uint32_t env,uint32_t diagnostic,float exposure){
        if(frame.objects.size()>max_objects||frame.particles.size()>ShowcaseWorld::max_particles||frame.lights.size()>128||frame.decals.size()>128||frame.volumes.size()>4||frame.streams.size()>max_water_streams)throw std::runtime_error("Showcase authored scene exceeds arena limits");
        environment_index=env%2;global_count=0;depth_transforms.clear();depth_batch_counts.fill(0);draws.clear();
        moving_shadows=std::any_of(frame.objects.begin(),frame.objects.end(),[](const auto&o){return o.movable&&o.material.surface==ShowcaseSurface::Opaque;});
        std::iota(decal_order.begin(),decal_order.begin()+frame.decals.size(),0);std::sort(decal_order.begin(),decal_order.begin()+frame.decals.size(),[&](size_t a,size_t b){const auto&x=frame.decals[a];const auto&y=frame.decals[b];return x.layer==y.layer?x.id<y.id:x.layer<y.layer;});
        base={};base.view_projection=perspective(58*pi/180,float(settings.width)/float(settings.height),.06f,180)*look_at(frame.eye,frame.target,frame.up);base.inverse_view_projection=inverse(base.view_projection);
        V3 sun=unit(V3{-.45f,.78f,.48f});for(const auto&light:frame.lights)if(light.type==LightType::Directional){sun=unit(light.direction)*-1;break;}const V3 safe_sun_up=std::abs(sun.y)>.95f?V3{0,0,1}:V3{0,1,0};const V3 sun_right=unit(cross(sun,safe_sun_up)),sun_up=unit(cross(sun_right,sun));
        const V3 ahead=unit(V3{frame.target.x-frame.eye.x,0,frame.target.z-frame.eye.z});
        auto cascade_projection=[&](float extent,float offset){V3 focus=frame.eye+ahead*offset;focus.y=0;float step=2*extent/float(settings.shadow_size);focus=focus+sun_right*(std::round(dot(focus,sun_right)/step)*step-dot(focus,sun_right))+sun_up*(std::round(dot(focus,sun_up)/step)*step-dot(focus,sun_up));return ortho(-extent,extent,-extent,extent,.1f,150)*look_at(focus+sun*65,focus,safe_sun_up);};
        base.directional_projection=cascade_projection(16,8);base.directional_far=cascade_projection(52,26);base.cascade={22,28,0,1};
        for(const auto&light:frame.lights)if(light.type==LightType::Directional){base.cascade.w=light.casts_shadow?1.0f:0.0f;break;}
        M4 mirror=identity();mirror.v[5]=-1;mirror.v[13]=2*frame.water_height;base.reflection_projection=base.view_projection*mirror;
        prepare_shadows(frame);
        base.camera_time={frame.eye.x,frame.eye.y,frame.eye.z,frame.time};base.sun_direction={-.45f,.78f,.48f,env?0.30f:1.45f};base.sun_color={.79f,.87f,1,env?0.0f:1.0f};base.params={float(diagnostic),exposure,0,0};base.fog={env?.018f:.010f,.12f,0,0};base.clip_plane={0,0,0,1};base.post.w=float(environments[environment_index].mips-1);
        V3 right=unit(cross(unit(frame.target-frame.eye),frame.up)),up=unit(cross(right,unit(frame.target-frame.eye)));base.camera_right={right.x,right.y,right.z,0};base.camera_up={up.x,up.y,up.z,0};base=dimensions(base,settings.width,settings.height);
        for(const auto&light:frame.lights)if(light.type==LightType::Directional){const V3 direction=unit(light.direction)*-1,color=light_color(light);base.sun_direction={direction.x,direction.y,direction.z,light_intensity_candela(light)};base.sun_color={color.x,color.y,color.z,env?0.0f:1.0f};base.cascade.z=light.mobility==LightMobility::Static&&frame.bake.valid?1.0f:0.0f;break;}
        for(const auto&light:frame.lights)if(light.type==LightType::Sky){const V3 color=light_color(light)*(light_intensity_candela(light)*pi);base.sky_color={color.x,color.y,color.z,light.mobility==LightMobility::Static&&frame.bake.valid?1.0f:0.0f};break;}
        base.volume_info={float(std::min(size_t(4),frame.volumes.size())),settings.effects==1?24.0f:48.0f,0,0};for(size_t i=0;i<std::min(size_t(4),frame.volumes.size());++i){const auto&v=frame.volumes[i];base.volume_inverse[i]=v.world_to_volume;base.volume_params[i]={float(v.asset),v.density,v.emission,v.time_offset};base.volume_color[i]={v.tint.x,v.tint.y,v.tint.z,v.playback_rate};}
        base.probe_origin={frame.probe_grid.origin.x,frame.probe_grid.origin.y,frame.probe_grid.origin.z,frame.irradiance_probes.empty()?0.0f:1.0f};
        base.probe_spacing={frame.probe_grid.spacing.x,frame.probe_grid.spacing.y,frame.probe_grid.spacing.z,0};base.probe_counts={float(frame.probe_grid.counts[0]),float(frame.probe_grid.counts[1]),float(frame.probe_grid.counts[2]),0};
        probe_pixels.fill({});for(size_t i=0;i<std::min(size_t(192),frame.irradiance_probes.size());++i){const auto&p=frame.irradiance_probes[i];for(unsigned j=0;j<6;++j){const auto b=p.baked_lobes[j],d=p.dynamic_lobes[j];probe_pixels[i*18+j]={b.x,b.y,b.z,1};probe_pixels[i*18+6+j]={d.x,d.y,d.z,1};probe_pixels[i*18+12+j]={p.distance_moments[j].x,p.distance_moments[j].y,p.sky_visibility[j],0};}}
        if(frame.water){for(unsigned z=0;z<65;++z)for(unsigned x=0;x<65;++x){const size_t i=z*65+x;const auto v=frame.water->vertex(x,z,frame.water_interpolation);const V3 p=frame.water->position(x,z,frame.water_interpolation);water_vertices[i]={{p.x,p.y,p.z},{v.normal.x,v.normal.y,v.normal.z},{1,0,0,-1},{float(x)/64,float(z)/64}};water_pixels[i]={v.foam,v.height,0,1};}}
        size_t object_slot=0,object_index=0;
        for(const auto&o:frame.objects){
            auto&cached=transform_cache[object_index++];const bool simulated_water=o.material.surface==ShowcaseSurface::Water&&frame.water;
            if(!cached.valid||cached.geometry!=o.geometry||std::memcmp(&cached.authored,&o.transform,sizeof(M4))){cached.authored=o.transform;cached.geometry=o.geometry;cached.model=o.transform*mesh_adjust[size_t(o.geometry)];cached.normal=transpose(inverse(cached.model));cached.valid=true;}
            size_t mesh_index=size_t(o.geometry);const auto&m=o.material.surface==ShowcaseSurface::Water&&frame.water?water_mesh:meshes[mesh_index];
            const size_t count=m.primitives.empty()?1:m.primitives.size();
            for(size_t p=0;p<count;++p){
                if(object_slot>=max_draws)throw std::runtime_error("Showcase primitive arena full");
                ObjectUniforms u{};u.model=simulated_water?identity():cached.model;u.normal_matrix=simulated_water?identity():cached.normal;u.base_color=o.material.base;u.material={o.material.metallic,o.material.roughness,o.material.normal_scale,o.material.wetness};u.emissive={o.material.emissive.x,o.material.emissive.y,o.material.emissive.z,o.material.foam};u.surface={float(o.material.surface),o.material.ior,o.material.thickness,o.material.distortion};u.tint={o.material.tint.x,o.material.tint.y,o.material.tint.z,1};
                const V3 sx{o.transform.v[0],o.transform.v[1],o.transform.v[2]},sz{o.transform.v[8],o.transform.v[9],o.transform.v[10]};u.object_options={float(o.material.texture),o.movable?1.0f:0.0f,std::sqrt(dot(sx,sx))*.5f,std::sqrt(dot(sz,sz))*.5f};
                // Curated prop meshes preserve their original material textures;
                // authored base tint and wetness still apply consistently.
                if(mesh_index>=4){u.object_options.x=3;const auto&mat=m.materials[m.primitives[p].material];u.base_color={u.base_color.x*mat.base.x,u.base_color.y*mat.base.y,u.base_color.z*mat.base.z,u.base_color.w*mat.base.w};u.material.x=mat.metallic;u.material.y=mat.roughness;}
                for(const auto&mapping:frame.baked_uv)if(mapping.object_id==o.id){std::copy(mapping.u.begin(),mapping.u.end(),u.lightmap_u);std::copy(mapping.v.begin(),mapping.v.end(),u.lightmap_v);u.lightmap_options.x=1;break;}
                u.mask_uv=o.material.mask.uv;u.mask_params={float(o.material.mask.texture),o.material.mask.wetness,o.material.mask.roughness,0};
                if(frame.bake.valid)for(const auto&surface:frame.baked_surfaces)if(surface.object_id==o.id){for(unsigned j=0;j<6;++j){const auto d=surface.direct[j],b=surface.indirect[j];u.baked_direct[j]={d.x,d.y,d.z,0};u.baked_indirect[j]={b.x,b.y,b.z,0};}break;}
                const V3 center{o.transform.v[12],o.transform.v[13],o.transform.v[14]};const float radius=.5f*std::sqrt(dot(sx,sx)+dot(sz,sz)+o.transform.v[4]*o.transform.v[4]+o.transform.v[5]*o.transform.v[5]+o.transform.v[6]*o.transform.v[6]);
                unsigned decals=0;for(size_t di=0;di<frame.decals.size();++di){const auto&decal=frame.decals[decal_order[di]];if(decals==8)break;if(!(decal.receiver_mask&o.decal_receiver_mask))continue;bool intersects=true;for(unsigned axis=0;axis<3;++axis){const auto&m=decal.world_to_decal;V3 row{m.v[axis],m.v[4+axis],m.v[8+axis]};if(std::abs(dot(row,center)+m.v[12+axis])>.5f+radius*std::sqrt(dot(row,row)))intersects=false;}if(!intersects)continue;u.decal_projection[decals]=decal.world_to_decal;u.decal_color[decals]=decal.color;u.decal_params[decals]={float(decal.texture),decal.normal,decal.roughness,decal.wetness};++decals;}u.mask_params.w=float(decals);
                select_lights(u,{o.transform.v[12],o.transform.v[13],o.transform.v[14]},frame,o.movable);
                std::memcpy(uniform_bytes.data()+object_base+object_slot*object_stride,&u,sizeof(u));
                auto maps=textures[size_t(std::clamp(u.object_options.x,0.0f,3.0f))];if(mesh_index>=4){const auto&mat=m.materials[m.primitives[p].material];maps={mat.albedo?mat.albedo:white,mat.normal?mat.normal:normal,mat.orm?mat.orm:orm};}if(u.surface.x>1.5f)maps={white,water_normal,white};
                draws.push_back({&m,m.primitives.empty()?0:m.primitives[p].first,m.primitives.empty()?m.index_count:m.primitives[p].count,object_slot,o.material.surface!=ShowcaseSurface::Opaque,o.double_sided||o.material.surface!=ShowcaseSurface::Opaque,o.movable,center,radius,maps});++object_slot;
            }
        }
        crown_count=0;
        if(frame.water)for(const auto&crown:frame.water->crowns())if(crown.strength>0){
            if(object_slot>=max_draws)throw std::runtime_error("Crown draw arena full");crown_geometry.update(crown,frame.water_interpolation);const size_t i=crown_count++;
            for(size_t v=0;v<crown_geometry.vertices.size();++v){const auto&vertex=crown_geometry.vertices[v];const V3 tangent=unit(cross(vertex.normal,{0,1,0}));crown_vertices[i][v]={{vertex.position.x,vertex.position.y,vertex.position.z},{vertex.normal.x,vertex.normal.y,vertex.normal.z},{tangent.x,tangent.y,tangent.z,1},{vertex.uv.x,vertex.uv.y}};}
            ObjectUniforms u{};u.model=u.normal_matrix=identity();u.base_color={.08f,.11f,.13f,1};u.material={0,.08f,.12f,0};u.surface={1,1.333f,.006f,.018f};u.tint={.8f,.9f,.96f,1};u.object_options={3,1,1,1};select_lights(u,crown.origin,frame,true);
            std::memcpy(uniform_bytes.data()+object_base+object_slot*object_stride,&u,sizeof(u));draws.push_back({&crown_meshes[i],0,SplashCrownGeometry::index_count,object_slot,true,true,true,crown.origin,crown.radius*3+crown.strength,{white,water_normal,white}});++object_slot;
        }
        stream_count=frame.streams.size();
        for(size_t i=0;i<stream_count;++i){if(object_slot>=max_draws)throw std::runtime_error("Stream draw arena full");const auto&stream=frame.streams[i];stream_geometry.update(stream);
            for(size_t v=0;v<stream_geometry.vertices.size();++v){const auto&vertex=stream_geometry.vertices[v];const V3 tangent=unit(cross(vertex.normal,std::abs(vertex.normal.y)>.9f?V3{1,0,0}:V3{0,1,0}));stream_vertices[i][v]={{vertex.position.x,vertex.position.y,vertex.position.z},{vertex.normal.x,vertex.normal.y,vertex.normal.z},{tangent.x,tangent.y,tangent.z,1},{vertex.uv.x,vertex.uv.y}};}
            ObjectUniforms u{};u.model=u.normal_matrix=identity();u.base_color={.02f,.07f,.08f,1};u.material={0,.065f,.15f,0};u.surface={1,1.333f,stream.radius*2,.015f};u.tint={.72f,.86f,.93f,1};u.object_options={3,1,1,1};
            const V3 end=WaterStreamGeometry::trajectory(stream,stream_geometry.flight_time),center=(stream.origin+end)*.5f;select_lights(u,center,frame,true);
            // A jet is one optical interface with its diameter as absorption
            // thickness. Its rear shell must not consume another peel layer.
            std::memcpy(uniform_bytes.data()+object_base+object_slot*object_stride,&u,sizeof(u));draws.push_back({&stream_meshes[i],0,WaterStreamGeometry::index_count,object_slot,true,false,true,center,std::sqrt(dot(end-stream.origin,end-stream.origin))*.5f+stream.radius,{white,water_normal,white}});++object_slot;
        }
        particle_uniform_index=object_slot;ObjectUniforms particle_uniform{};select_lights(particle_uniform,frame.eye,frame);std::memcpy(uniform_bytes.data()+object_base+object_slot*object_stride,&particle_uniform,sizeof(particle_uniform));
        particle_order.resize(frame.particles.size());std::iota(particle_order.begin(),particle_order.end(),0u);
        std::sort(particle_order.begin(),particle_order.end(),[&](uint32_t a,uint32_t b){float da=dot(frame.particles[a].position-frame.eye,frame.particles[a].position-frame.eye),db=dot(frame.particles[b].position-frame.eye,frame.particles[b].position-frame.eye);return da==db?a<b:da>db;});
        particle_data.clear();for(uint32_t i:particle_order){const auto&p=frame.particles[i];if(!visible(base.view_projection,p.position,p.size*4))continue;
            V3 illumination{.11f,.11f,.11f};
            if(p.kind==ShowcaseParticleKind::Smoke||p.kind==ShowcaseParticleKind::Steam||p.kind==ShowcaseParticleKind::Dust){
                // Evaluate at each sprite centre; emitters in another room never
                // inherit the camera's selected light list. The list is bounded.
                for(const auto&l:frame.lights){if(l.type==LightType::Sky||l.type==LightType::Directional)continue;V3 delta=p.position-l.position;const float d=std::sqrt(dot(delta,delta));if(d>=l.radius)continue;float a=std::pow(std::max(0.0f,1-d/l.radius),2)/(l.units==LightIntensityUnit::Legacy?1+d*d:std::max(d*d,.01f));if(l.type==LightType::Spot||l.spot_cosine>0){float cone=dot(delta*(1/std::max(d,.001f)),unit(l.direction));a*=std::clamp((cone-l.spot_cosine)/std::max(l.inner_cone_cosine-l.spot_cosine,.001f),0.0f,1.0f);}if(l.type==LightType::Rect)a*=std::max(0.0f,dot(delta*(1/std::max(d,.001f)),unit(l.direction)));illumination=illumination+light_color(l)*(light_intensity_candela(l)*a*.22f);}
            }
            particle_data.push_back({{p.position.x,p.position.y,p.position.z,p.size},p.color,{p.velocity.x,p.velocity.y,p.velocity.z,float(p.kind)},{std::clamp(p.age/std::max(p.lifetime,.001f),0.0f,.999f),p.rotation,0,0},{illumination.x,illumination.y,illumination.z,0}});
        }
    }
    kgfx_bindings bindings(size_t global_slot,size_t object_slot){
        kgfx_bindings b{};b.uniforms[0]=uniforms;b.uniform_offset[0]=global_slot*global_stride;b.uniform_size[0]=sizeof(Globals);b.uniforms[1]=uniforms;b.uniform_offset[1]=object_base+object_slot*object_stride;b.uniform_size[1]=sizeof(ObjectUniforms);return b;
    }
    void texture(kgfx_bindings&b,unsigned slot,kgfx_texture*t,kgfx_sampler*s=nullptr){b.textures[slot]=t;b.samplers[slot]=s?s:clamp;}
    void transition(kgfx_texture*t,kgfx_access access){r.check(kgfx_texture_transition(device,t,access),"showcase texture transition");}
    void begin(kgfx_texture*color,kgfx_texture*depth,uint32_t w,uint32_t h,V4 clear={},bool load=false){
        if(color)transition(color,KGFX_ACCESS_ATTACHMENT);if(depth)transition(depth,KGFX_ACCESS_ATTACHMENT);
        kgfx_pass_desc p{};p.color=color;p.depth=depth;p.width=w;p.height=h;p.color_load=load?KGFX_LOAD:KGFX_CLEAR;p.depth_load=load?KGFX_LOAD:KGFX_CLEAR;p.clear_color[0]=clear.x;p.clear_color[1]=clear.y;p.clear_color[2]=clear.z;p.clear_color[3]=clear.w;p.clear_depth=1;
        r.check(kgfx_pass_begin(device,&p),"begin showcase pass");kgfx_viewport(device,0,0,w,h);kgfx_scissor(device,0,0,w,h);
    }
    void end(kgfx_texture*sampled=nullptr){kgfx_pass_end(device);if(sampled)transition(sampled,KGFX_ACCESS_SAMPLED);}
    void fullscreen(kgfx_pipeline*p,size_t global_slot,kgfx_texture*source=nullptr,kgfx_texture*secondary=nullptr){
        auto b=bindings(global_slot,particle_uniform_index);if(source)texture(b,0,source);if(secondary)texture(b,1,secondary);
        if(p==ao_pipeline||p==blur_pipeline)texture(b,10,targets->linear_depth,nearest);
        if(p==tone_pipeline){texture(b,2,targets->shafts);texture(b,10,targets->linear_depth,nearest);texture(b,12,targets->peel[0],nearest);texture(b,13,targets->peel[1],nearest);texture(b,14,targets->peel[2],nearest);}
        if(p==volume_pipeline){texture(b,9,targets->spots,nearest);for(unsigned i=0;i<3;++i){texture(b,6+i,volume_atlases[i]);texture(b,3+i,volume_distant[i]);}texture(b,10,targets->linear_depth,nearest);texture(b,12,targets->peel[0],nearest);texture(b,13,targets->peel[1],nearest);}
        if(p==volume_composite[0]||p==volume_composite[1]){texture(b,10,targets->linear_depth,nearest);texture(b,12,targets->peel[0],nearest);texture(b,13,targets->peel[1],nearest);}
        if(p==shafts_pipeline){texture(b,8,targets->spots,nearest);texture(b,10,targets->linear_depth,nearest);}
        kgfx_bind_pipeline(device,p);kgfx_bind_resources(device,&b);kgfx_draw(device,0,3,1);
    }
    enum class Pass {Depth,Shadow,Opaque,Reflection,Peel,Transparent};
    void prepare_depth_batches(size_t slot,const M4&projection){
        for(const auto&mesh:meshes)for(unsigned two=0;two<2;++two){const size_t first=depth_transforms.size();
            for(const auto&d:draws)if(!d.moving&&!d.transparent&&d.first==0&&d.mesh==&mesh&&d.two_sided==bool(two)&&visible(projection,d.center,d.radius)){
                if(depth_transforms.size()==max_objects*max_globals)throw std::runtime_error("Static depth instance arena full");
                M4 model;std::memcpy(&model,uniform_bytes.data()+object_base+d.object*object_stride,sizeof(model));depth_transforms.push_back(model);
            }
            const auto count=depth_transforms.size()-first;if(count)depth_batches[slot][depth_batch_counts[slot]++]={&mesh,first*sizeof(M4),uint32_t(count),bool(two)};
        }
    }
    void geometry(Pass pass,size_t global_slot,unsigned layer=0,int moving_filter=-1){
        const bool transparent=pass==Pass::Peel||pass==Pass::Transparent;
        auto&t=*targets;const auto&e=environments[environment_index];
        Globals globals{};std::memcpy(&globals,uniform_bytes.data()+global_slot*global_stride,sizeof(globals));
        const bool depth_pass=pass==Pass::Depth||pass==Pass::Shadow;
        if(depth_pass&&moving_filter!=1){
            // Upload once before any draws. Rewriting a buffer already in use
            // can serialize the native GL driver even for disjoint ranges.
            for(unsigned i=0;i<depth_batch_counts[global_slot];++i){const auto&batch=depth_batches[global_slot][i];
                auto b=bindings(global_slot,particle_uniform_index);b.vertex[0]=batch.mesh->vertices;b.vertex[1]=depth_instances;b.vertex_offset[1]=batch.offset;b.index=batch.mesh->indices;b.index_size=4;
                kgfx_bind_pipeline(device,pass==Pass::Depth?depth_instanced_pipeline[batch.two_sided]:shadow_instanced_pipeline[batch.two_sided]);kgfx_bind_resources(device,&b);kgfx_draw_indexed(device,0,batch.mesh->index_count,batch.count);
            }
        }
        for(const auto&d:draws){
            if(depth_pass&&!d.moving)continue;
            if((pass==Pass::Depth||pass==Pass::Shadow)&&d.first!=0)continue;
            if(d.transparent!=transparent||(moving_filter>=0&&d.moving!=(moving_filter==1))||!visible(globals.view_projection,d.center,d.radius))continue;
            auto b=bindings(global_slot,d.object);b.vertex[0]=d.mesh->vertices;b.index=d.mesh->indices;b.index_size=4;
            kgfx_pipeline*p=nullptr;
            if(pass==Pass::Depth)p=depth_pipeline[d.two_sided];
            else if(pass==Pass::Shadow)p=shadow_pipeline[d.two_sided];
            else if(pass==Pass::Peel){p=peel_pipeline[d.two_sided];texture(b,10,t.linear_depth,nearest);texture(b,12,layer?t.peel[layer-1]:white,nearest);}
            else {
                p=pass==Pass::Reflection?reflected_pipeline[d.two_sided]:transparent?transparent_pipeline[settings.samples==4][d.two_sided]:surface_pipeline[settings.samples==4][d.two_sided];
                const auto&maps=d.maps;
                texture(b,0,maps.albedo,repeat);texture(b,1,maps.normal,repeat);texture(b,2,maps.orm,repeat);texture(b,3,grime,repeat);
                texture(b,4,e.irradiance);texture(b,5,e.specular,mip_clamp);texture(b,6,lookup);texture(b,7,t.sun,nearest);texture(b,8,t.spots,nearest);texture(b,9,t.ao[0]);texture(b,10,t.linear_depth,nearest);
                if(transparent){texture(b,11,t.snapshot);texture(b,12,t.peel[0],nearest);texture(b,13,t.peel[1],nearest);texture(b,14,t.reflection);texture(b,15,water_state);}
                else {texture(b,11,probe_texture,nearest);texture(b,12,surface_masks);texture(b,13,lightmap);}
            }
            kgfx_bind_pipeline(device,p);kgfx_bind_resources(device,&b);kgfx_draw_indexed(device,(pass==Pass::Depth||pass==Pass::Shadow)?0:d.first,(pass==Pass::Depth||pass==Pass::Shadow)?d.mesh->index_count:d.count,1);
        }
    }
    void sky(size_t slot,unsigned samples){auto b=bindings(slot,particle_uniform_index);texture(b,5,environments[environment_index].specular,mip_clamp);kgfx_bind_pipeline(device,sky_pipeline[samples==4]);kgfx_bind_resources(device,&b);kgfx_draw(device,0,3,1);}
    void particles(size_t slot){
        if(particle_data.empty())return;
        auto&t=*targets;auto b=bindings(slot,particle_uniform_index);b.vertex[1]=particle_buffer;
        for(unsigned i=0;i<3;++i)texture(b,6+i,volume_atlases[i]);
        texture(b,0,fire);texture(b,1,smoke);texture(b,2,spray);texture(b,3,sparks);texture(b,4,environments[environment_index].irradiance);texture(b,10,t.linear_depth,nearest);texture(b,12,t.peel[0],nearest);texture(b,13,t.peel[1],nearest);
        kgfx_bind_pipeline(device,particle_pipeline[settings.samples==4]);kgfx_bind_resources(device,&b);kgfx_draw(device,0,6,uint32_t(particle_data.size()));
    }
    void snapshot(kgfx_texture*destination){
        auto&t=*targets;
        if(settings.samples>1)r.check(kgfx_texture_resolve(device,t.hdr,destination),"resolve showcase HDR");
        else {transition(t.hdr,KGFX_ACCESS_COPY_SOURCE);transition(destination,KGFX_ACCESS_COPY_DESTINATION);r.check(kgfx_texture_copy(device,t.hdr,destination,0,0,0,0,settings.width,settings.height),"snapshot showcase HDR");}
        transition(destination,KGFX_ACCESS_SAMPLED);
    }
    kgfx_texture*render(const ShowcaseFrame&frame,uint32_t env,uint32_t diagnostic,float exposure){
        measurements.next_frame();measurements.begin(0);
        prepare(frame,env,diagnostic,exposure);auto&t=*targets;
        const size_t main_slot=global(base);
        auto g=base;g.view_projection=base.directional_projection;const size_t sun_slot=global(g);g.view_projection=base.directional_far;const size_t sun_far_slot=global(g);
        std::array<size_t,32>spot_slots{};for(size_t s=0;s<shadow_count;++s)if(shadow_lights[s]>=0){g=base;g.view_projection=base.spot_projection[s];spot_slots[s]=global(g);}
        g=dimensions(base,t.rw,t.rh);g.view_projection=base.reflection_projection;g.inverse_view_projection=inverse(g.view_projection);g.camera_time.y=2*frame.water_height-frame.eye.y;g.params.z=1;g.clip_plane={0,1,0,-frame.water_height+.02f};const size_t reflection_slot=global(g);
        std::array<size_t,3>volume_slots{};for(unsigned i=0;i<3;++i){g=dimensions(base,t.vw,t.vh);g.params.w=float(i);volume_slots[i]=global(g);}
        std::array<size_t,3>layer_slots{};for(unsigned i=0;i<3;++i){g=base;g.params.w=float(i);layer_slots[i]=global(g);}
        g=dimensions(base,t.aw,t.ah);const size_t ao_slot=global(g);g.post.x=1.0f/float(t.aw);const size_t ao_h=global(g);g.post.x=0;g.post.y=1.0f/float(t.ah);const size_t ao_v=global(g);
        const size_t shaft_slot=global(dimensions(base,t.sw,t.sh));
        g=dimensions(base,t.bw,t.bh);g.post.x=1.0f/float(t.bw);g.post.y=0;g.post.z=1;const size_t bloom_h=global(g);g.post.x=0;g.post.y=1.0f/float(t.bh);g.post.z=0;const size_t bloom_v=global(g);
        prepare_depth_batches(main_slot,base.view_projection);
        if(base.cascade.w>.5f){if(sun_dirty[0])prepare_depth_batches(sun_slot,base.directional_projection);if(sun_dirty[1])prepare_depth_batches(sun_far_slot,base.directional_far);}
        for(size_t s=0;s<shadow_count;++s)if(shadow_lights[s]>=0&&shadow_dirty[s])prepare_depth_batches(spot_slots[s],base.spot_projection[s]);
        if(!depth_transforms.empty())r.check(kgfx_buffer_update(device,depth_instances,0,depth_transforms.data(),depth_transforms.size()*sizeof(M4)),"upload static depth batches");
        r.check(kgfx_buffer_update(device,uniforms,0,uniform_bytes.data(),global_count*global_stride),"upload showcase pass uniforms");
        r.check(kgfx_buffer_update(device,uniforms,object_base,uniform_bytes.data()+object_base,(particle_uniform_index+1)*object_stride),"upload showcase object uniforms");
        if(!particle_data.empty())r.check(kgfx_buffer_update(device,particle_buffer,0,particle_data.data(),particle_data.size()*sizeof(ParticleInstance)),"upload ordered showcase particles");
        r.check(kgfx_texture_upload(device,probe_texture,0,0,probe_pixels.data(),18*sizeof(V4)),"upload irradiance probes");
        for(size_t i=0;i<crown_count;++i)r.check(kgfx_buffer_update(device,crown_meshes[i].vertices,0,crown_vertices[i].data(),sizeof(crown_vertices[i])),"upload impact crown");
        for(size_t i=0;i<stream_count;++i)r.check(kgfx_buffer_update(device,stream_meshes[i].vertices,0,stream_vertices[i].data(),sizeof(stream_vertices[i])),"upload coherent water stream");
        if(frame.water){r.check(kgfx_buffer_update(device,water_mesh.vertices,0,water_vertices.data(),sizeof(water_vertices)),"upload water surface");r.check(kgfx_texture_upload(device,water_state,0,0,water_pixels.data(),65*sizeof(V4)),"upload impact foam");}
        // Single-sample radial depth makes soft particles and SSAO independent
        // of backend MSAA depth-resolve support.
        begin(t.linear_depth,t.linear_z,settings.width,settings.height,{1000,0,0,1});geometry(Pass::Depth,main_slot);end(t.linear_depth);
        begin(t.ao[0],nullptr,t.aw,t.ah,{1,1,1,1});fullscreen(ao_pipeline,ao_slot);end(t.ao[0]);
        begin(t.ao[1],nullptr,t.aw,t.ah,{1,1,1,1});fullscreen(blur_pipeline,ao_h,t.ao[0]);end(t.ao[1]);
        begin(t.ao[0],nullptr,t.aw,t.ah,{1,1,1,1});fullscreen(blur_pipeline,ao_v,t.ao[1]);end(t.ao[0]);
        measurements.end(0);measurements.begin(1);
        begin(nullptr,t.sun,settings.shadow_size*2,settings.shadow_size*2,{},sun_initialized);
        for(unsigned cascade_index=0;cascade_index<2;++cascade_index)for(unsigned moving=0;moving<2;++moving){
            if(moving&&!moving_shadows&&!previous_moving_shadows&&!sun_dirty[cascade_index])continue;
            if(!moving&&!sun_dirty[cascade_index])continue;const auto slot=cascade_index?sun_far_slot:sun_slot;
            const int32_t x=int32_t(cascade_index*settings.shadow_size),y=int32_t(moving*settings.shadow_size);
            kgfx_viewport(device,x,y,settings.shadow_size,settings.shadow_size);kgfx_scissor(device,x,y,settings.shadow_size,settings.shadow_size);fullscreen(shadow_clear_pipeline,slot);
            if(base.cascade.w>.5f)geometry(Pass::Shadow,slot,0,int(moving));
        }end(t.sun);sun_initialized=true;
        // Static and moving depth occupy separate halves of the same atlas.
        // Updating a tile never clears any other light's persistent allocation.
        begin(nullptr,t.spots,t.local_tile*8,t.local_tile*8,{},shadow_initialized);
        for(size_t s=0;s<shadow_count;++s)if(shadow_lights[s]>=0)for(unsigned moving=0;moving<2;++moving){
            if(moving&&!moving_shadows&&!previous_moving_shadows&&!shadow_dirty[s])continue;
            if(!moving&&!shadow_dirty[s])continue;unsigned slot=unsigned(s)+moving*32;const auto x=int32_t((slot%8)*t.local_tile),y=int32_t((slot/8)*t.local_tile);
            kgfx_viewport(device,x,y,t.local_tile,t.local_tile);kgfx_scissor(device,x,y,t.local_tile,t.local_tile);fullscreen(shadow_clear_pipeline,spot_slots[s]);
            kgfx_viewport(device,x+2,y+2,t.local_tile-4,t.local_tile-4);kgfx_scissor(device,x+2,y+2,t.local_tile-4,t.local_tile-4);geometry(Pass::Shadow,spot_slots[s],0,int(moving));
        }end(t.spots);shadow_initialized=true;previous_moving_shadows=moving_shadows;shadow_cache.rendered();
        measurements.end(1);measurements.begin(5);
        begin(t.shafts,nullptr,t.sw,t.sh);if(diagnostic==0)fullscreen(shafts_pipeline,shaft_slot);end(t.shafts);
        measurements.end(5);measurements.begin(2);
        // Initialize all sampled targets before the opaque/reflection pipelines
        // bind them. In particular a scene without water never samples old data.
        begin(t.snapshot,nullptr,settings.width,settings.height,{0,0,0,1});end(t.snapshot);
        for(unsigned i=0;i<3;++i){begin(t.peel[i],t.peel_z,settings.width,settings.height,{1000,0,0,1});if(i<2||diagnostic==7)geometry(Pass::Peel,layer_slots[i],i);end(t.peel[i]);}
        begin(t.reflection,t.reflection_z,t.rw,t.rh,{.03f,.055f,.08f,1});
        if(frame.has_water){sky(reflection_slot,1);geometry(Pass::Reflection,reflection_slot);}end(t.reflection);
        measurements.end(2);measurements.begin(3);
        begin(t.hdr,t.depth,settings.width,settings.height,{0,0,0,1});sky(main_slot,settings.samples);geometry(Pass::Opaque,main_slot);end();
        measurements.end(3);measurements.begin(4);
        // Two immutable snapshots and three particle bands keep smoke, sparks,
        // and fire visible behind either pane without sampling an attachment.
        for(int band=2;band>=0;--band){
            if(!frame.volumes.empty()){begin(t.volume,nullptr,t.vw,t.vh);fullscreen(volume_pipeline,volume_slots[size_t(band)]);end(t.volume);begin(t.hdr,t.depth,settings.width,settings.height,{},true);fullscreen(volume_composite[settings.samples==4],layer_slots[size_t(band)],t.volume);end();}
            begin(t.hdr,t.depth,settings.width,settings.height,{},true);particles(layer_slots[size_t(band)]);end();
            if(band){snapshot(t.snapshot);begin(t.hdr,t.depth,settings.width,settings.height,{},true);geometry(Pass::Transparent,layer_slots[size_t(band-1)]);end();}
        }
        measurements.end(4);measurements.begin(6);
        snapshot(t.resolved);
        begin(t.bloom[0],nullptr,t.bw,t.bh);fullscreen(bloom_pipeline,bloom_h,t.resolved);end(t.bloom[0]);
        begin(t.bloom[1],nullptr,t.bw,t.bh);fullscreen(bloom_pipeline,bloom_v,t.bloom[0]);end(t.bloom[1]);
        begin(t.ldr,nullptr,settings.width,settings.height,{0,0,0,1});fullscreen(tone_pipeline,main_slot,t.resolved,t.bloom[1]);end(t.ldr);measurements.end(6);return t.ldr;
    }
};
ShowcaseRenderer::ShowcaseRenderer(kgfx_device*d,const std::filesystem::path&root,const GraphicsSettings&s):impl_(std::make_unique<Impl>(d,root,s)){}
ShowcaseRenderer::~ShowcaseRenderer()=default;
void ShowcaseRenderer::resize(const GraphicsSettings&s){impl_->resize(s);}
kgfx_texture*ShowcaseRenderer::render(const ShowcaseFrame&frame,uint32_t environment,uint32_t diagnostic,float exposure){return impl_->render(frame,environment,diagnostic,exposure);}
const ShowcaseTimings&ShowcaseRenderer::timings()const{return impl_->measurements.values;}
}
