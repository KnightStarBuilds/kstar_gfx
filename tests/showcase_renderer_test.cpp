// SPDX-License-Identifier: MIT
// Exercise the production pass graph with tiny authored probes, not screenshots
// of a whole map. Assertions inspect completed-GPU pixels for optical ordering.
#include "showcase_renderer.hpp"
#include <kstar_gfx/opengl.h>
#define SDL_MAIN_HANDLED
#include <SDL.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <vector>

namespace {
using namespace demo;
constexpr uint32_t extent=256;
void check(bool value,const char*message){if(!value)throw std::runtime_error(message);}
void ok(kgfx_result result){check(result==KGFX_OK,"GPU operation failed");}
struct Image {
    std::vector<uint8_t>pixels;
    std::array<double,3>region(int x,int y,int radius=6)const{
        std::array<double,3>sum{};unsigned n=0;
        for(int yy=std::max(0,y-radius);yy<std::min(int(extent),y+radius+1);++yy)for(int xx=std::max(0,x-radius);xx<std::min(int(extent),x+radius+1);++xx){++n;for(unsigned c=0;c<3;++c)sum[c]+=pixels[(size_t(yy)*extent+size_t(xx))*4+c];}
        for(auto&x:sum)x/=n;return sum;
    }
};
ShowcaseObject pane(float z,ShowcaseSurface surface=ShowcaseSurface::Glass){
    ShowcaseObject o;o.geometry=ShowcaseGeometry::Plane;o.transform=translate({0,0,z})*rotate_x(pi/2)*scale({5,1,5});o.material.surface=surface;o.material.base={.06f,.07f,.08f,1};o.material.texture=ShowcaseTexture::None;o.material.roughness=.06f;o.material.distortion=0;o.material.tint={1,1,1};o.double_sided=true;return o;
}
ShowcaseObject background(){auto o=pane(-3,ShowcaseSurface::Opaque);o.material.base={0,0,0,1};o.material.emissive={.035f,.035f,.035f};return o;}
ShowcaseParticle particle(V3 p,ShowcaseParticleKind kind=ShowcaseParticleKind::Fire){ShowcaseParticle s;s.position=s.previous=p;s.color={1,.16f,.02f,.95f};s.kind=kind;s.size=1.6f;s.age=.25f;s.lifetime=1;return s;}
struct Rig {
    SDL_Window*window=nullptr;SDL_GLContext context=nullptr;kgfx_device*device=nullptr;
    std::unique_ptr<ShowcaseRenderer>renderer;
    std::vector<ShowcaseObject>objects;
    std::vector<ShowcaseParticle>particles;
    std::vector<ShowcaseLight>lights;
    ShowcaseFrame frame;
    explicit Rig(const std::filesystem::path&resources){
        SDL_SetMainReady();check(SDL_Init(SDL_INIT_VIDEO)==0,SDL_GetError());
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION,4);SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION,1);SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,SDL_GL_CONTEXT_PROFILE_CORE);
#if defined(__APPLE__)
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS,SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);
#endif
        window=SDL_CreateWindow("showcase GPU probes",0,0,int(extent),int(extent),SDL_WINDOW_OPENGL|SDL_WINDOW_HIDDEN);check(window,SDL_GetError());context=SDL_GL_CreateContext(window);check(context,SDL_GetError());
        kgfx_gl_config gl{SDL_GL_GetProcAddress,nullptr,nullptr};kgfx_device_desc desc{KGFX_ABI_VERSION,extent,extent,3,&gl,nullptr,nullptr};ok(kgfx_device_create(&desc,&device));
        renderer=std::make_unique<ShowcaseRenderer>(device,resources,GraphicsSettings{extent,extent,1,1024,1});reset();
    }
    ~Rig(){renderer.reset();if(device){kgfx_device_wait_idle(device);kgfx_device_destroy(device);}if(context)SDL_GL_DeleteContext(context);if(window)SDL_DestroyWindow(window);SDL_Quit();}
    void reset(){objects={background()};particles.clear();lights.clear();frame={};frame.eye={0,0,4};frame.target={0,0,0};}
    Image capture(unsigned diagnostic=0,float exposure=1){
        frame.objects=objects;frame.particles=particles;frame.lights=lights;ok(kgfx_frame_begin(device));auto*ldr=renderer->render(frame,0,diagnostic,exposure);kgfx_readback*readback=nullptr;ok(kgfx_readback_begin(device,ldr,&readback));auto completion=kgfx_submit(device,0);check(completion!=0,"GPU submission failed");ok(kgfx_wait(device,completion,30'000'000'000ull));kgfx_readback_data data{};ok(kgfx_readback_map(device,readback,&data));check(data.width==extent&&data.height==extent,"Probe dimensions changed");Image image;image.pixels.resize(size_t(extent)*extent*4);for(uint32_t y=0;y<extent;++y)std::copy_n(static_cast<const uint8_t*>(data.pixels)+size_t(y)*data.row_pitch,extent*4,image.pixels.data()+size_t(y)*extent*4);kgfx_readback_destroy(device,readback);return image;
    }
};
void transparency(Rig&r,unsigned samples){
    r.renderer->resize({extent,extent,samples,1024,1});r.reset();
    auto baseline=r.capture().region(128,128);r.particles={particle({0,0,-1})};auto direct=r.capture().region(128,128);
    check(direct[0]>baseline[0]+20,"Fire must contribute HDR light and bloom");
    r.objects.push_back(pane(.5f));auto one=r.capture().region(128,128);
    check(one[0]>baseline[0]+15,"Fire behind one glass layer disappeared");
    r.objects.push_back(pane(-.4f));auto two=r.capture().region(128,128);
    check(two[0]>baseline[0]+10,"Fire behind two glass layers disappeared");
    r.particles.clear();auto empty=r.capture().region(128,128);
    check(two[0]>empty[0]+10,"Two-layer composite must include behind-glass particle band");
    auto layers=r.capture(6).region(128,128);check(layers[2]>layers[1]+50,"Two visible interfaces must select two-layer diagnostic");
    r.objects.push_back(pane(-.7f));auto overflow=r.capture(7).region(128,128);check(overflow[0]>230&&overflow[1]<20&&overflow[2]>170,"Third glass interface must be diagnosed");
    r.reset();r.particles={particle({0,0,-3.5f})};auto hidden=r.capture().region(128,128);check(std::abs(hidden[0]-baseline[0])<4,"Opaque depth must reject rear particles");
    r.particles={particle({0,0,-2.95f})};auto soft=r.capture().region(128,128);check(soft[0]<direct[0]-10,"Soft particle intersection must fade against opaque depth");
    r.reset();r.particles={particle({0,0,-1})};ShowcaseParticle smoke=particle({0,0,0},ShowcaseParticleKind::Smoke);smoke.size=2.5f;smoke.color={.01f,.01f,.01f,1};r.particles.push_back(smoke);auto front_smoke=r.capture().region(128,128);check(front_smoke[0]<direct[0]-5,"Foreground smoke must attenuate fire in sorted particle order");
    std::printf("PASS transparency %ux MSAA: direct %.1f one %.1f two %.1f hidden %.1f soft %.1f smoke %.1f\n",samples,direct[0],one[0],two[0],hidden[0],soft[0],front_smoke[0]);
}
void glass_materials(Rig&r){
    r.reset();r.objects[0].material.emissive={.6f,.6f,.6f};r.objects.push_back(pane(0));
    const auto clear=r.capture().region(128,128);
    r.objects.back().material.tint={.2f,1,.2f};r.objects.back().material.thickness=.15f;
    const auto tinted=r.capture().region(128,128);
    check(tinted[1]>tinted[0]+25&&tinted[0]<clear[0]-25,"Glass thickness/tint must absorb transmitted wavelengths");
    r.reset();ShowcaseObject bar;bar.transform=translate({0,0,-2})*scale({.035f,2,.04f});bar.material.base={0,0,0,1};bar.material.emissive={.6f,.6f,.6f};r.objects.push_back(bar);r.objects.push_back(pane(0));
    const auto sharp=r.capture().region(128,128,0);r.objects.back().material.roughness=.9f;
    const auto frosted=r.capture().region(128,128,0);
    check(sharp[0]>frosted[0]+8,"Frosted transmission must blur a narrow background fixture");
    std::printf("PASS glass materials: clear red %.1f tinted red/green %.1f/%.1f sharp/frost %.1f/%.1f\n",clear[0],tinted[0],tinted[1],sharp[0],frosted[0]);
}
void close_grazing_layers(Rig&r){
    std::array<double,2>between_delta{},behind_delta{};
    for(unsigned sample_index=0;sample_index<2;++sample_index){
        const unsigned samples=sample_index?4:1;
        r.renderer->resize({extent,extent,samples,1024,1});r.reset();
        // Center column 128 crosses x=0: the two 80-degree panes are only
        // 3 cm apart there, less than one pixel's depth variation. A broad
        // depth tolerance could therefore draw either pane in the wrong band.
        const float center_offset=-4*std::tan(29*pi/180)/float(extent);
        r.frame.eye.x=r.frame.target.x=center_offset;
        auto near=pane(.015f),far=pane(-.015f);
        near.transform=translate({0,0,.015f})*rotate_y(80*pi/180)*rotate_x(pi/2)*scale({5,1,5});
        far.transform=translate({0,0,-.015f})*rotate_y(80*pi/180)*rotate_x(pi/2)*scale({5,1,5});
        near.material.tint={.05f,1,1};far.material.tint={1,.05f,1};
        near.material.thickness=far.material.thickness=.08f;
        r.objects.push_back(near);r.objects.push_back(far);
        auto green_column=[](const Image&image){double value=0;for(int y=116;y<=140;++y)value+=image.region(128,y,0)[1];return value/25;};
        const double empty=green_column(r.capture());
        auto fire=particle({0,0,0});fire.color={.02f,1,.02f,1};fire.size=.8f;r.particles={fire};
        between_delta[sample_index]=green_column(r.capture())-empty;
        r.particles[0].position.z=r.particles[0].previous.z=-.08f;
        behind_delta[sample_index]=green_column(r.capture())-empty;
        // The near cyan pane passes green; the rear magenta pane absorbs it.
        // The same particle must be brighter between panes than behind both.
        check(between_delta[sample_index]>15,"Particle between close grazing glass layers disappeared");
        check(between_delta[sample_index]>behind_delta[sample_index]+12,"Close grazing glass selected the wrong particle layer");
        std::printf("PASS close grazing glass %ux MSAA: between/rear green delta %.1f/%.1f\n",samples,between_delta[sample_index],behind_delta[sample_index]);
    }
    check(std::abs(between_delta[0]-between_delta[1])<8,"Multisampling changed interior transmission through close grazing glass");
}
void resize_transaction(Rig&r){
    r.reset();auto before=r.capture();bool rejected=false;
    try{r.renderer->resize({extent,0,4,1024,1});}catch(const std::exception&){rejected=true;}
    check(rejected,"Invalid replacement dimensions must be rejected");
    auto after=r.capture();check(before.pixels==after.pixels,"Failed target replacement must retain the active render targets");
    std::puts("PASS failed resize preserves completed-GPU output");
}
void reflection(Rig&r){
    r.reset();r.objects.clear();r.frame.eye={0,1.5f,5};r.frame.target={0,0,0};r.frame.has_water=true;r.frame.water_height=-.5f;
    ShowcaseObject water;water.geometry=ShowcaseGeometry::Plane;water.transform=translate({0,-.5f,-1})*scale({8,1,8});water.material.surface=ShowcaseSurface::Water;water.material.base={.01f,.025f,.04f,1};water.material.roughness=.05f;water.material.distortion=0;water.material.foam=0;water.double_sided=true;r.objects.push_back(water);
    auto empty=r.capture();
    ShowcaseObject glow;glow.geometry=ShowcaseGeometry::Cube;glow.transform=translate({-1,1,-1})*scale({.9f,.9f,.9f});glow.material.base={0,0,0,1};glow.material.emissive={30,0,0};r.objects.push_back(glow);auto reflected=r.capture();
    // The mirrored point is left and below the pool plane. Compare the same
    // pixels with/without the red fixture, so environment exposure is irrelevant.
    auto project=[&](V3 point){M4 vp=perspective(58*pi/180,1,.06f,180)*look_at(r.frame.eye,r.frame.target,{0,1,0});float x=vp.v[0]*point.x+vp.v[4]*point.y+vp.v[8]*point.z+vp.v[12],y=vp.v[1]*point.x+vp.v[5]*point.y+vp.v[9]*point.z+vp.v[13],w=vp.v[3]*point.x+vp.v[7]*point.y+vp.v[11]*point.z+vp.v[15];return std::array<int,2>{int((x/w*.5f+.5f)*extent),int((.5f-y/w*.5f)*extent)};};
    auto point=project({-1,-2,-1});auto before=empty.region(point[0],point[1],10),after=reflected.region(point[0],point[1],10);check(after[0]>before[0]+12,"Planar reflection must retain left/right orientation and reflected geometry");
    auto opposite_before=empty.region(int(extent)-point[0],point[1],10),opposite_after=reflected.region(int(extent)-point[0],point[1],10);check(after[0]-before[0]>opposite_after[0]-opposite_before[0]+8,"Planar reflection was horizontally flipped");
    std::printf("PASS planar reflection: projected (%d,%d), red %.1f -> %.1f\n",point[0],point[1],before[0],after[0]);
}
ShowcaseLight disabled_sun(){ShowcaseLight l;l.id=9000;l.type=LightType::Directional;l.intensity=0;l.units=LightIntensityUnit::Lux;return l;}
ShowcaseObject receiver(V3 center,V3 normal){
    const V3 right=unit(cross(normal,std::abs(normal.y)>.9f?V3{0,0,1}:V3{0,1,0})),forward=cross(right,normal);
    M4 basis=identity();for(unsigned i=0;i<3;++i){basis.v[i]=(&right.x)[i];basis.v[4+i]=(&normal.x)[i];basis.v[8+i]=(&forward.x)[i];}
    ShowcaseObject o;o.id=100;o.geometry=ShowcaseGeometry::Plane;o.transform=translate(center)*basis*scale({7,1,7});o.material.base={.35f,.35f,.35f,1};o.material.roughness=.7f;o.material.texture=ShowcaseTexture::None;o.double_sided=true;return o;
}
void point_shadow_faces(Rig&r){
    r.renderer->resize({extent,extent,1,1024,1});
    // The last pair straddles the +X/+Z face boundary with the same physical
    // receiver and blocker arrangement, detecting an unshadowed seam.
    const std::array<V3,8> directions{{{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1},unit({1.001f,0,1}),unit({1,0,1.001f})}};
    std::array<double,8> shadow_values{};
    for(size_t face=0;face<directions.size();++face){
        r.reset();const V3 axis=directions[face],normal=axis*-1;
        const V3 side=unit(cross(normal,std::abs(normal.y)>.9f?V3{0,0,1}:V3{0,1,0}));
        r.frame.target=axis*3;r.frame.eye=r.frame.target+normal*4+side*2;r.frame.up=cross(side,normal);
        r.objects={receiver(r.frame.target,normal)};
        ShowcaseLight l;l.id=42;l.type=LightType::Point;l.mobility=LightMobility::Stationary;l.units=LightIntensityUnit::Candela;l.intensity=35;l.radius=12;l.casts_shadow=true;l.shadow_bias=.001f;
        r.lights={disabled_sun(),l};
        const auto lit=r.capture(0,.35f).region(128,128,4);
        ShowcaseObject blocker;blocker.id=101;blocker.transform=translate(axis*1.5f)*scale({.65f,.65f,.65f});blocker.material.base={.02f,.02f,.02f,1};r.objects.push_back(blocker);
        const auto shadow=r.capture(0,.35f).region(128,128,4);
        shadow_values[face]=shadow[0];
        std::printf("Point face %zu lit/shadow %.1f/%.1f\n",face,lit[0],shadow[0]);
        check(lit[0]>shadow[0]+15,"Each of six Point atlas faces must shadow its receiver");
        // Stationary emission edits keep static visibility but immediately alter
        // direct lighting. The cached blocker must continue to suppress light.
        r.lights[1].color={.05f,1,.05f};r.lights[1].intensity=70;
        const auto cached=r.capture(0,.35f).region(128,128,4);
        // Reclassify the blocker as moving, then translate it out of the same
        // cached Stationary light. The dynamic half of every face must update.
        r.objects.back().movable=true;r.objects.back().geometry_revision++;
        const auto moving_shadow=r.capture(0,.35f).region(128,128,4);
        r.objects.back().transform=translate(axis*1.5f+side*2)*scale({.65f,.65f,.65f});r.objects.back().geometry_revision++;
        const auto restored=r.capture(0,.35f).region(128,128,4);
        std::printf("Point face %zu cached/moving/restored green %.1f/%.1f/%.1f\n",face,cached[1],moving_shadow[1],restored[1]);
        check(cached[1]<restored[1]-15,"Stationary color/intensity edits must retain cached blocker visibility");
        check(std::abs(cached[1]-moving_shadow[1])<3,"Static and moving visibility must agree before the blocker moves");
        check(restored[1]>moving_shadow[1]+20,"Moving blockers must invalidate dynamic Point visibility on every face");
        check(restored[1]>restored[0]+20,"Stationary color edits must change the unshadowed direct illumination");
    }
    check(std::abs(shadow_values[6]-shadow_values[7])<5,"Adjacent Point shadow faces must agree across the cube seam");
    std::puts("PASS Point six-face shadows and seam, stationary emission edits, moving blockers");
}
void rect_light_shape(Rig&r){
    r.renderer->resize({extent,extent,1,1024,1});r.reset();r.objects={receiver({0,0,-3},{0,0,1})};
    ShowcaseLight light;light.id=51;light.type=LightType::Rect;light.mobility=LightMobility::Stationary;light.units=LightIntensityUnit::Candela;light.intensity=15;light.radius=12;light.direction={0,0,-1};light.right={1,0,0};light.width=2.4f;light.height=.25f;
    r.lights={disabled_sun(),light};
    const auto front=r.capture(0,.4f).region(128,128,4);
    r.lights[1].direction={0,0,1};const auto rear=r.capture(0,.4f).region(128,128,4);
    std::printf("Rect front/back %.1f/%.1f\n",front[0],rear[0]);
    check(front[0]>rear[0]+15,"Rect emitters must reject illumination from their back side");
    r.lights[1].direction={0,0,-1};r.objects[0].material.metallic=1;r.objects[0].material.roughness=.12f;
    const auto horizontal=r.capture(0,.4f);
    r.lights[1].right={0,1,0};const auto vertical=r.capture(0,.4f);
    const double hx=horizontal.region(149,128,2)[0],hy=horizontal.region(128,149,2)[0];
    const double vx=vertical.region(149,128,2)[0],vy=vertical.region(128,149,2)[0];
    std::printf("Rect specular horizontal %.1f/%.1f vertical %.1f/%.1f\n",hx,hy,vx,vy);
    check(hx>hy+8&&vy>vx+8,"Rect specular highlight orientation must follow its emitting surface");
    r.lights[1].right={1,0,0};r.lights[1].width=.2f;const auto narrow=r.capture(0,.4f);
    check(hx>narrow.region(149,128,2)[0]+8,"Rect source width must affect the specular highlight footprint");
    r.lights[1].width=2.4f;r.objects[0].material.roughness=.75f;const auto rough=r.capture(0,.4f);
    check(horizontal.region(128,128,2)[0]>rough.region(128,128,2)[0]+10,"Roughness must broaden and reduce the rectangular highlight peak");
    r.lights[1].mobility=LightMobility::Static;r.frame.bake.valid=true;
    const auto baked_metal=r.capture(0,.4f).region(128,128,2);
    check(std::abs(baked_metal[0]-rough.region(128,128,2)[0])<2,"Static bake must retain runtime metallic specular without diffuse duplication");
    r.objects[0].material.surface=ShowcaseSurface::Glass;r.objects[0].material.metallic=0;r.objects[0].material.roughness=.12f;
    const auto baked_glass=r.capture(0,.4f).region(128,128,2);r.frame.bake.valid=false;
    const auto dynamic_glass=r.capture(0,.4f).region(128,128,2);
    check(std::abs(baked_glass[0]-dynamic_glass[0])<2,"Static bake must not suppress direct highlights on refractive receivers");
    std::puts("PASS Rect backside, source width/orientation and rough specular response");
}
void directional_cascades(Rig&r){
    r.reset();r.objects={receiver({0,0,-3},{0,0,1})};r.frame.target={0,0,-3};
    ShowcaseLight sun;sun.id=61;sun.type=LightType::Directional;sun.mobility=LightMobility::Stationary;sun.units=LightIntensityUnit::Lux;sun.direction={0,0,-1};sun.intensity=2;sun.casts_shadow=true;r.lights={sun};
    ShowcaseObject blocker;blocker.id=62;blocker.transform=translate({0,0,-1.5f})*scale({.8f,.8f,.8f});r.objects.push_back(blocker);
    uint64_t dirty_draws=0;for(float distance:{20.f,25.f,30.f}){r.frame.eye=r.frame.target+unit(V3{.5f,0,1})*distance;kgfx_stats before{},after{};kgfx_get_stats(r.device,&before);const auto shadow=r.capture(4).region(128,128,1);kgfx_get_stats(r.device,&after);dirty_draws=after.draws-before.draws;check(shadow[0]<80,"Near, blended and far Directional cascades must preserve blocker visibility");}
    kgfx_stats first{},second{},third{};kgfx_get_stats(r.device,&first);r.capture(4);kgfx_get_stats(r.device,&second);
    check(second.draws-first.draws<dirty_draws,"Unchanged Directional cascades must skip static shadow submissions");
    r.lights[0].color={.1f,1,.1f};r.lights[0].intensity=4;r.capture(4);kgfx_get_stats(r.device,&third);
    check(second.draws-first.draws==third.draws-second.draws,"Stationary Directional emission edits must reuse static cascade visibility");
    r.objects.back().movable=true;++r.objects.back().geometry_revision;r.capture(4);
    r.objects.back().transform=translate({3,0,-1.5f})*scale({.8f,.8f,.8f});++r.objects.back().geometry_revision;
    // The shadow diagnostic still passes through ACES: fully lit white is 232.
    check(r.capture(4).region(128,128,1)[0]>225,"Moving blockers must refresh directional visibility independently of static layers");
    r.objects.back().transform=blocker.transform;++r.objects.back().geometry_revision;r.lights[0].casts_shadow=false;
    check(r.capture(4).region(128,128,1)[0]>225,"Directional shadow controls must disable visibility sampling");
    std::puts("PASS Directional cascade overlap, cached emission edits and moving blockers");
}
void sky_direct(Rig&r){
    r.reset();r.objects={receiver({0,0,-3},{0,0,1})};
    ShowcaseLight sky;sky.type=LightType::Sky;sky.mobility=LightMobility::Stationary;sky.units=LightIntensityUnit::Luminance;sky.intensity=1.5f;r.lights={disabled_sun(),sky};
    std::array<IrradianceProbe,8>probes{};for(auto&p:probes)for(auto&moment:p.distance_moments)moment={30,900};
    r.frame.probe_grid={{-4,-4,-5},{8,8,8},{2,2,2}};r.frame.irradiance_probes=probes;
    const auto closed=r.capture().region(128,128,4);
    for(auto&p:probes)p.sky_visibility.fill(1);const auto open=r.capture().region(128,128,4);
    check(open[0]>closed[0]+30,"Sky direct lighting must use traced probe visibility independently of bounce");
    r.lights[1].color={.05f,1,.05f};const auto green=r.capture().region(128,128,4);
    check(green[1]>green[0]+30,"Stationary Sky color edits must update direct illumination");
    std::printf("PASS Sky direct visibility and emission edits: closed/open %.1f/%.1f\n",closed[0],open[0]);
}
void volume_composition(Rig&r,unsigned samples){
    r.renderer->resize({extent,extent,samples,1024,1});r.reset();
    r.objects[0].material.emissive={.8f,.8f,.8f};
    const double clear=r.capture().region(128,128,40)[0];
    std::array<ShowcaseVolume,4> volumes{};
    for(size_t i=0;i<volumes.size();++i){
        auto&v=volumes[i];v.id=uint32_t(i+1);v.asset=ShowcaseVolumeAsset::ChimneySmoke;v.emission=0;v.tint={0,0,0};v.density=4.f+float(i)*.5f;
        v.world_to_volume=inverse(translate({float(i)*.08f,0,0})*scale({4,4,3}));
    }
    r.frame.volumes={volumes.data(),1};const auto one=r.capture();
    r.frame.volumes=volumes;const auto four=r.capture();
    const double single=one.region(128,128,40)[0],overlap=four.region(128,128,40)[0];
    std::printf("Volumes %ux clear/one/four %.1f/%.1f/%.1f\n",samples,clear,single,overlap);
    check(clear>single+5&&single>overlap+5,"Overlapping hero volumes must accumulate extinction along the same ray");
    std::reverse(volumes.begin(),volumes.end());const auto reordered=r.capture();
    unsigned max_error=0;for(size_t i=0;i<four.pixels.size();++i)max_error=std::max(max_error,unsigned(std::abs(int(four.pixels[i])-int(reordered.pixels[i]))));
    check(max_error<=2,"Overlapping density must be independent of volume draw order");
    // Enter the front of the plume's shared bounds, before its dense slices;
    // the geometric centre lies in clear air in this real simulation frame.
    r.frame.eye={-.3f,0,1.35f};r.frame.target={-.3f,0,-3};r.frame.volumes={};
    const double inside_clear=r.capture().region(128,128,30)[0];r.frame.volumes=volumes;
    const double inside=r.capture().region(128,128,30)[0];
    std::printf("Volume inside clear/smoke %.1f/%.1f\n",inside_clear,inside);
    check(inside_clear>inside+5,"Camera-inside volume rays must start at the eye and accumulate density");
    r.frame.eye={0,0,4};r.frame.target={0,0,0};
    for(auto&v:volumes)v.world_to_volume=inverse(translate({0,0,-5})*scale({1,1,1}));
    const auto occluded=r.capture().region(128,128,40);
    check(std::abs(occluded[0]-clear)<2,"Opaque geometry must terminate volume integration behind its depth");
    r.reset();r.particles={particle({0,0,-1})};
    const auto ember=r.capture().region(128,128,12);
    volumes[0].density=30;volumes[0].world_to_volume=inverse(translate({.25f,0,1})*scale({3,3,2}));r.frame.volumes={volumes.data(),1};
    const auto smoky_ember=r.capture().region(128,128,12);
    std::printf("Ember clear/smoke %.1f/%.1f\n",ember[0],smoky_ember[0]);
    check(ember[0]>smoky_ember[0]+8,"Volumetric smoke must attenuate fine emissive particles behind it");
    std::printf("PASS volumes %ux: camera-inside %.1f/%.1f, ember %.1f/%.1f, overlap order max error %u\n",samples,inside_clear,inside,ember[0],smoky_ember[0],max_error);
}
void wet_masks_and_decals(Rig&r,unsigned samples){
    r.renderer->resize({extent,extent,samples,1024,1});r.reset();r.objects={receiver({0,0,-3},{0,0,1})};
    const auto dry=r.capture(10).region(128,128,4),rough=r.capture(11).region(128,128,4);
    r.objects[0].material.mask={SurfaceMask::Leak,{0,0,.5f,.5f},1,0};
    const auto authored=r.capture(10).region(128,128,4),smooth=r.capture(11).region(128,128,4);
    check(authored[0]>dry[0]+100,"An independent material UV mask must create a wet coating");
    check(rough[0]>smooth[0]+35,"A wet material mask must lower the lighting roughness");
    r.objects[0].material.mask.uv={0,0,.02f,.02f};const auto outside=r.capture(10).region(128,128,4);
    check(authored[0]>outside[0]+100,"Changing authored UVs must move the irregular wet boundary independently of geometry");
    r.objects[0].material.mask={};
    ShowcaseDecal decal;decal.id=7;decal.receiver_mask=2;decal.texture=SurfaceMask::Leak;decal.wetness=1;
    decal.world_to_decal=inverse(translate({0,0,-3})*rotate_x(pi/2)*scale({4,.2f,4}));r.frame.decals={&decal,1};
    const auto rejected=r.capture(10).region(128,128,4);
    check(std::abs(rejected[0]-dry[0])<2,"Decal receiver filtering must reject unrelated geometry");
    r.objects[0].decal_receiver_mask=2;const auto projected=r.capture(10).region(128,128,4),coated=r.capture(11).region(128,128,4);
    check(projected[0]>dry[0]+100&&rough[0]>coated[0]+35,"Projected wet decals must change wetness and roughness before lighting");
    std::printf("PASS wet masks/decals %ux: dry/authored/projected %.1f/%.1f/%.1f rough/smooth %.1f/%.1f\n",samples,dry[0],authored[0],projected[0],rough[0],smooth[0]);
}
}
int main(int argc,char**argv){try{check(argc==2,"Usage: kgfx_showcase_renderer_test RESOURCE_ROOT");Rig rig(argv[1]);transparency(rig,1);transparency(rig,4);glass_materials(rig);close_grazing_layers(rig);reflection(rig);resize_transaction(rig);point_shadow_faces(rig);rect_light_shape(rig);directional_cascades(rig);sky_direct(rig);volume_composition(rig,1);volume_composition(rig,4);wet_masks_and_decals(rig,1);wet_masks_and_decals(rig,4);return 0;}catch(const std::exception&e){std::fprintf(stderr,"Showcase renderer probe failed: %s\n",e.what());return 1;}}
