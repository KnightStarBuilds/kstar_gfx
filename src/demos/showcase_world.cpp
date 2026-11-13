#include "showcase_world.hpp"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <stdexcept>

namespace demo {
namespace {
ShowcaseMaterial concrete(V4 color={.3f,.33f,.34f,1}){ShowcaseMaterial m;m.base=color;m.texture=ShowcaseTexture::Concrete;m.roughness=.82f;return m;}
ShowcaseMaterial metal(V4 color={.12f,.18f,.2f,1},float roughness=.35f){ShowcaseMaterial m;m.base=color;m.texture=ShowcaseTexture::Metal;m.metallic=.82f;m.roughness=roughness;return m;}
ShowcaseMaterial painted(V4 color){auto m=metal(color,.43f);m.metallic=.35f;return m;}
ShowcaseMaterial glow(V3 color){ShowcaseMaterial m;m.base={.05f,.05f,.05f,1};m.emissive=color;m.roughness=.3f;return m;}
float square_distance(V3 a,V3 b){return dot(a-b,a-b);}
V3 mix(V3 a,V3 b,float t){return a+(b-a)*t;}
constexpr float eye_height=1.7f,body_radius=.28f;
constexpr double tick_seconds=1.0/60.0;
}

bool ShowcaseWorld::supports_scene(std::string_view scene){return scene=="facility"||scene=="glass_lab"||scene=="waterworks"||scene=="boiler_room"||scene=="lighting_baked"||scene=="lighting_dynamic";}
ShowcaseWorld::ShowcaseWorld(std::string_view scene,uint32_t seed,uint32_t limit,bool bake_static){
    if(!supports_scene(scene))throw std::invalid_argument("Unknown showcase scene");
    scene_=scene=="lighting_baked"?Scene::LightingBaked:scene=="lighting_dynamic"?Scene::LightingDynamic:scene=="glass_lab"?Scene::GlassLab:scene=="waterworks"?Scene::Waterworks:scene=="boiler_room"?Scene::BoilerRoom:Scene::Facility;
    state_.seed=seed;particle_limit_=std::min(limit,max_particles);
    objects_.reserve(512);lights_.reserve(24);light_intensities_.reserve(24);light_positions_.reserve(24);blockers_.reserve(128);stations_.reserve(8);ground_.reserve(8);emitters_.reserve(32);glass_objects_.reserve(16);fixture_objects_.reserve(24);fire_objects_.reserve(4);moving_lights_.reserve(4);
    particles_.reserve(max_particles);render_particles_.reserve(max_particles);
    light_colors_.reserve(24);animated_fixtures_.reserve(12);moving_occluders_.reserve(4);decals_.reserve(128);volumes_.reserve(4);streams_.reserve(max_water_streams);
    build();reset();
    if(lighting_scene())lighting_.initialize(objects_,lights_,{{-5.25f,.65f,-4.8f},{1.5f,1.8f,2.4f},{8,4,5}},scene_==Scene::LightingBaked&&bake_static);
}
std::string_view ShowcaseWorld::scene() const{
    switch(scene_){case Scene::Facility:return "facility";case Scene::GlassLab:return "glass_lab";case Scene::Waterworks:return "waterworks";case Scene::BoilerRoom:return "boiler_room";case Scene::LightingBaked:return "lighting_baked";case Scene::LightingDynamic:return "lighting_dynamic";}return {};
}
std::string_view ShowcaseWorld::sequence() const{
    switch(sequence_){case Sequence::Idle:return "idle";case Sequence::Interact:return "interact";case Sequence::Tour:return "tour";}return {};
}
void ShowcaseWorld::object(ShowcaseGeometry geometry,V3 center,V3 dimensions,const ShowcaseMaterial& material,bool collide,float angle){
    objects_.push_back({geometry,translate(center)*rotate_y(angle)*scale(dimensions),material,false});
    if(collide){
        // Rotate the authored box into a conservative world-space AABB. Boxes
        // are static; the moving player is a vertical capsule projected to XZ.
        float c=std::abs(std::cos(angle)),s=std::abs(std::sin(angle));
        V3 half{(dimensions.x*c+dimensions.z*s)*.5f,dimensions.y*.5f,(dimensions.z*c+dimensions.x*s)*.5f};
        if(center.y-half.y<eye_height&&center.y+half.y>.3f)blockers_.push_back({center-half,center+half});
    }
}
void ShowcaseWorld::light(V3 position,V3 color,float intensity,float radius,bool shadow,V3 direction,float cone){
    lights_.push_back({position,radius,color,intensity,unit(direction),cone,shadow});light_intensities_.push_back(intensity);light_positions_.push_back(position);light_colors_.push_back(color);
    auto&l=lights_.back();l.id=lights_.size();l.type=cone>=0?LightType::Spot:LightType::Point;l.inner_cone_cosine=cone>=0?std::min(.98f,cone+.17f):.9f;
}
void ShowcaseWorld::pipe(V3 from,V3 to,float radius){
    V3 d=to-from;float length=std::sqrt(dot(d,d));if(length<.01f)return;
    V3 y=d*(1/length),x=unit(cross(std::abs(y.z)<.9f?V3{0,0,1}:V3{1,0,0},y)),z=cross(x,y);
    M4 basis=identity();basis.v[0]=x.x;basis.v[1]=x.y;basis.v[2]=x.z;basis.v[4]=y.x;basis.v[5]=y.y;basis.v[6]=y.z;basis.v[8]=z.x;basis.v[9]=z.y;basis.v[10]=z.z;
    auto pipe_material=painted({.24f,.31f,.29f,1});
    objects_.push_back({ShowcaseGeometry::Cylinder,translate((from+to)*.5f)*basis*scale({radius*2,length,radius*2}),pipe_material});
    auto joint=metal({.16f,.18f,.18f,1},.22f);
    for(float t:{.08f,.92f})objects_.push_back({ShowcaseGeometry::Cylinder,translate(from+d*t)*basis*scale({radius*2.65f,.09f,radius*2.65f}),joint});
}
void ShowcaseWorld::railing(V3 from,V3 to){
    const auto yellow=painted({.72f,.46f,.07f,1});V3 delta=to-from;float length=std::sqrt(dot(delta,delta));
    for(float t=0;t<length+.01f;t+=1.5f){V3 p=from+delta*(std::min(t,length)/length);object(ShowcaseGeometry::Cube,p+V3{0,.52f,0},{.055f,1.04f,.055f},yellow);}
    for(float height:{.5f,1.0f})object(ShowcaseGeometry::Cube,(from+to)*.5f+V3{0,height,0},{.055f,.055f,length+.1f},yellow,false,std::atan2(delta.x,delta.z));
    // The rail bars are deliberately one continuous collision volume.
    V3 lo{std::min(from.x,to.x)-.045f,from.y,std::min(from.z,to.z)-.045f},hi{std::max(from.x,to.x)+.045f,from.y+1.05f,std::max(from.z,to.z)+.045f};
    blockers_.push_back({lo,hi});
}
void ShowcaseWorld::floor_marks(V3 center,float length){
    const auto yellow=painted({.75f,.51f,.09f,1});
    for(float x:{-1.25f,1.25f})object(ShowcaseGeometry::Cube,center+V3{x,.013f,0},{.07f,.008f,length},yellow);
    for(float z=-length*.5f;z<length*.5f;z+=1.5f)object(ShowcaseGeometry::Cube,center+V3{0,.017f,z},{.16f,.012f,.6f},yellow);
}
void ShowcaseWorld::room(V3 center,V3 dimensions,bool roof,bool basin){
    auto wall=concrete({.29f,.32f,.34f,1}),floor=concrete({.19f,.23f,.24f,1});floor.wetness=.42f;
    if(basin){
        // The floor is cut around the pool, not hidden directly below water.
        // A real one-metre depth keeps soft intersections and shoreline foam
        // local to the basin edges and provides visible absorption depth.
        object(ShowcaseGeometry::Cube,center+V3{-6.225f,-.18f,0},{1.55f,.35f,12},floor);
        object(ShowcaseGeometry::Cube,center+V3{3.975f,-.18f,0},{6.05f,.35f,12},floor);
        for(float z:{-5.f,5.f})object(ShowcaseGeometry::Cube,center+V3{-2.25f,-.18f,z},{6.4f,.35f,2},floor);
        auto tiles=concrete({.13f,.23f,.25f,1});tiles.texture=ShowcaseTexture::Tile;tiles.roughness=.7f;
        object(ShowcaseGeometry::Cube,center+V3{-2.25f,-1.18f,0},{6.4f,.35f,8},tiles);
        for(float x:{-5.45f,.95f})object(ShowcaseGeometry::Cube,center+V3{x,-.52f,0},{.12f,1,8},tiles);
        for(float z:{-4.f,4.f})object(ShowcaseGeometry::Cube,center+V3{-2.25f,-.52f,z},{6.4f,1,.12f},tiles);
    }else object(ShowcaseGeometry::Cube,center+V3{0,-.18f,0},{dimensions.x,.35f,dimensions.z},floor);
    for(float x:{-.5f,.5f}){
        object(ShowcaseGeometry::Cube,center+V3{x*dimensions.x,dimensions.y*.5f,0},{.24f,dimensions.y,dimensions.z},wall,true);
        for(float z=-dimensions.z*.5f;z<=dimensions.z*.5f;z+=4){
            object(ShowcaseGeometry::Cube,center+V3{x*(dimensions.x-.4f),dimensions.y*.5f,z},{.25f,dimensions.y,.3f},metal());
        }
        object(ShowcaseGeometry::Cube,center+V3{x*(dimensions.x-.27f),.6f,0},{.08f,.08f,dimensions.z},painted({.63f,.37f,.06f,1}));
    }
    // Doorways are 3.2 m wide and aligned along the facility's central route.
    for(float z:{-.5f,.5f})for(float x:{-.25f,.25f})object(ShowcaseGeometry::Cube,center+V3{x*(dimensions.x+3.2f),dimensions.y*.5f,z*dimensions.z},{(dimensions.x-3.2f)*.5f,dimensions.y,.2f},wall,true);
    for(float z=-dimensions.z*.5f;z<=dimensions.z*.5f;z+=4)object(ShowcaseGeometry::Cube,center+V3{0,dimensions.y,z},{dimensions.x,.25f,.3f},metal());
    if(roof)object(ShowcaseGeometry::Cube,center+V3{0,dimensions.y+.15f,0},{dimensions.x,.15f,dimensions.z},wall);
    if(!basin)floor_marks(center,dimensions.z);
}
void ShowcaseWorld::station(V3 position,Action action){
    const auto body=painted({.16f,.23f,.26f,1});
    object(ShowcaseGeometry::PowerBox,position+V3{0,.85f,0},{.62f,1.7f,.48f},body,true);
    object(ShowcaseGeometry::Cube,position+V3{0,1.24f,.25f},{.43f,.31f,.025f},glow({.05f,1.4f,1.1f}));
    object(ShowcaseGeometry::Sphere,position+V3{.18f,.9f,.28f},{.065f,.065f,.065f},glow({2,.1f,.02f}));
    stations_.push_back({position+V3{0,1.2f,.4f},action});
}
void ShowcaseWorld::yard(V3 center){
    volumes_.push_back({uint32_t(volumes_.size()+1),ShowcaseVolumeAsset::SmallCampfire,inverse(translate(center+V3{-4.8f,.9f,-2.4f})*scale({1.3f,1.8f,1.3f})),2.5f,5,0});
    auto asphalt=concrete({.12f,.16f,.19f,1});asphalt.wetness=.8f;
    object(ShowcaseGeometry::Cube,center+V3{0,-.15f,0},{16,.3f,14},asphalt);
    floor_marks(center,12);
    for(float x:{-7.8f,7.8f}){
        object(ShowcaseGeometry::Cube,center+V3{x,1.15f,0},{.25f,2.3f,14},concrete(),true);
        for(float z:{-4.f,4.f}){
            object(ShowcaseGeometry::Cube,center+V3{x*.88f,2.6f,z},{.16f,5.2f,.16f},metal(),true);
            object(ShowcaseGeometry::Cube,center+V3{x*.88f,5.1f,z},{1.2f,.12f,.4f},glow({2.8f,3.8f,5}));
            light(center+V3{x*.88f,4.9f,z},{.6f,.78f,1},12,12,true,{x>0?-.4f:.4f,-1,0},.72f);
        }
    }
    object(ShowcaseGeometry::Cube,center+V3{-4,1.4f,-2},{3.5f,2.8f,4},painted({.16f,.26f,.3f,1}),true);
    for(float z=-3.8f;z<0;z+=.4f)object(ShowcaseGeometry::Cube,center+V3{-2.22f,1.4f,z},{.04f,2.55f,.035f},metal({.2f,.29f,.3f,1}));
    for(int n=0;n<4;++n)object(ShowcaseGeometry::Barrel,center+V3{4.3f+float(n%2)*1.05f,.6f,-2+float(n/2)*1.15f},{.82f,1.2f,.82f},painted({.38f,.17f,.08f,1}),true);
    for(float z:{2.5f,4.5f}){object(ShowcaseGeometry::Cube,center+V3{4.4f,.55f,z},{1.6f,1.1f,1.6f},painted({.22f,.27f,.25f,1}),true);object(ShowcaseGeometry::Cube,center+V3{4.4f,1.16f,z},{1.7f,.12f,1.7f},metal());}
    station(center+V3{-2.7f,0,3.8f},Action::Lights);
    // Loading-bay gantry, painted clearance bands, and bollards frame the route.
    for(float x:{-1.85f,1.85f}){
        object(ShowcaseGeometry::Cube,center+V3{x,2.45f,-6.7f},{.19f,4.9f,.32f},painted({.62f,.32f,.055f,1}));
        object(ShowcaseGeometry::Cube,center+V3{x,1.95f,-6.48f},{.08f,3.4f,.035f},glow({2.3f,.65f,.1f}));
        object(ShowcaseGeometry::Cylinder,center+V3{x,.48f,-5.7f},{.23f,.96f,.23f},painted({.76f,.48f,.055f,1}),true);
    }
    object(ShowcaseGeometry::Cube,center+V3{0,4.65f,-6.7f},{4.1f,.5f,.36f},painted({.12f,.21f,.24f,1}));
    for(int n=0;n<7;++n)object(ShowcaseGeometry::Cube,center+V3{float(n-3)*.46f,4.66f,-6.48f},{.24f,.18f,.025f},glow({.12f,1.1f,1.7f}));
    light(center+V3{0,3.9f,-6.2f},{.12f,.6f,1},8,8);
    emitters_.push_back({ShowcaseParticleKind::Rain,center+V3{0,8,0},{15,0,13},{-.8f,-13,.25f},280});
}
void ShowcaseWorld::glass_room(V3 center,bool enclosed){
    if(scene_!=Scene::Facility)for(float x:{-4.9f,-1.6f,1.7f})volumes_.push_back({uint32_t(volumes_.size()+1),ShowcaseVolumeAsset::ChimneySmoke,inverse(translate(center+V3{x,1.55f,-2.9f})*scale({1.1f,2.1f,1.1f})),1.2f,0,x,.4f});
    if(enclosed)room(center,{14,5,10});
    auto frame=metal({.09f,.14f,.17f,1},.2f);
    for(int n=0;n<3;++n){
        V3 p=center+V3{-4.3f+float(n)*3.3f,2,-1.4f};
        ShowcaseMaterial glass;glass.surface=ShowcaseSurface::Glass;glass.base={.8f,.93f,.95f,1};glass.roughness=.025f;glass.tint={.82f,.95f,.98f};glass.thickness=.025f;glass.distortion=.012f;
        glass_objects_.push_back(objects_.size());objects_.push_back({ShowcaseGeometry::Plane,translate(p)*rotate_x(pi/2)*scale({2.9f,1,3.2f}),glass,true});
        for(float x:{-1.49f,1.49f})object(ShowcaseGeometry::Cube,p+V3{x,0,0},{.09f,3.5f,.13f},frame,true);
        for(float y:{-1.68f,1.68f})object(ShowcaseGeometry::Cube,p+V3{0,y,0},{3.07f,.12f,.15f},frame);
        blockers_.push_back({p+V3{-1.45f,-1.6f,-.025f},p+V3{1.45f,1.6f,.025f}});
        object(ShowcaseGeometry::Cube,p+V3{0,-1.8f,0},{3.2f,.35f,.75f},concrete(),true);
        object(ShowcaseGeometry::Sphere,p+V3{0,-.7f,-1.4f},{.8f,.8f,.8f},metal({.73f,.31f,.09f,1},.1f));
        moving_lights_.push_back(lights_.size());light(p+V3{0,1.6f,-1.5f},{.2f+float(n)*.35f,.5f,.9f-float(n)*.3f},12,6);
        emitters_.push_back({ShowcaseParticleKind::Smoke,p+V3{-.6f,-1.5f,-1.5f},{.25f,0,.25f},{.05f,.4f,0},6});
        emitters_.push_back({ShowcaseParticleKind::Spark,p+V3{.65f,-1,-1.6f},{.04f,.1f,.04f},{0,1.3f,0},5});
    }
    // A second, smaller inner pane makes the supported two-interface case
    // visible. The other bays stay single-layer from every walking position.
    ShowcaseMaterial inner;inner.surface=ShowcaseSurface::Glass;inner.roughness=.025f;inner.tint={.9f,.97f,1};
    glass_objects_.push_back(objects_.size());objects_.push_back({ShowcaseGeometry::Plane,translate(center+V3{-1,1.95f,-2.15f})*rotate_x(pi/2)*scale({1.6f,1,2.3f}),inner,true});
    blockers_.push_back({center+V3{-1.8f,.8f,-2.18f},center+V3{-.2f,3.1f,-2.12f}});
    emitters_.push_back({ShowcaseParticleKind::Dust,center+V3{-1,1.7f,.5f},{4,2.5f,.3f},{.04f,.015f,0},9});
    emitters_.push_back({ShowcaseParticleKind::Smoke,center+V3{-4.5f,.3f,.7f},{.15f,0,.15f},{.04f,.4f,0},4});
    station(center+V3{4.8f,0,2.1f},Action::Glass);
    pipe(center+V3{-6,3.8f,-3.5f},center+V3{6,3.8f,-3.5f},.12f);
    for(float x:{-4.5f,0.f,4.5f}){
        fixture_objects_.push_back(objects_.size());object(ShowcaseGeometry::Cube,center+V3{x,4.3f,.8f},{2.2f,.09f,.2f},glow({2.6f,3.3f,4}));
        light(center+V3{x,4,.8f},{.64f,.82f,1},8,9,true,{0,-1,-.2f},.67f);
    }
    emitters_.push_back({ShowcaseParticleKind::Dust,center+V3{0,2.5f,1},{11,4,7},{.025f,.012f,.015f},5});
}
void ShowcaseWorld::water_room(V3 center){
    volumes_.push_back({uint32_t(volumes_.size()+1),ShowcaseVolumeAsset::ChimneySmoke,inverse(translate(center+V3{-4.5f,3.35f,-3})*scale({1.6f,2.2f,1.6f})),.65f,0,.4f,.55f,{.75f,.88f,.93f}});
    room(center,{14,6,12},false,true);
    water_center_=center+V3{-2.25f,.035f,0};has_water_=true;
    ShowcaseMaterial water;water.surface=ShowcaseSurface::Water;water.base={.045f,.23f,.26f,1};water.tint={.24f,.67f,.65f};water.roughness=.06f;water.ior=1.333f;water.thickness=1.3f;water.distortion=.025f;water.foam=.65f;
    objects_.push_back({ShowcaseGeometry::Plane,translate(water_center_)*scale({6.4f,1,8}),water,true});
    // Pool observation glass gives a reachable water-through-glass view. This
    // pane plus the horizontal water surface is exactly two interfaces.
    ShowcaseMaterial observation;observation.surface=ShowcaseSurface::Glass;observation.roughness=.025f;observation.tint={.9f,.97f,.98f};
    glass_objects_.push_back(objects_.size());objects_.push_back({ShowcaseGeometry::Plane,translate(center+V3{1.42f,1.4f,-1.3f})*rotate_y(pi/2)*rotate_x(pi/2)*scale({4.4f,1,2.5f}),observation,true});
    for(float z:{-3.55f,.95f})object(ShowcaseGeometry::Cube,center+V3{1.42f,1.4f,z},{.1f,2.7f,.1f},metal());
    for(float x:{-5.65f,1.15f})object(ShowcaseGeometry::Cube,center+V3{x,.22f,0},{.3f,.44f,8.5f},concrete(),true);
    for(float z:{-4.2f,4.2f})object(ShowcaseGeometry::Cube,center+V3{-2.25f,.22f,z},{7.1f,.44f,.3f},concrete(),true);
    // Raised catwalk and ramp share their exact authored walking heights.
    auto steel=metal({.14f,.2f,.22f,1},.42f);steel.wetness=.55f;
    object(ShowcaseGeometry::Cube,center+V3{3.6f,.43f,-.4f},{3.8f,.3f,9.6f},steel);
    floor_marks(center+V3{3.6f,.58f,-.4f},9.3f);
    ground_.push_back({center+V3{1.7f,0,-5.2f},center+V3{5.5f,0,4.4f},.58f,.58f});
    M4 ramp=translate(center+V3{3.6f,.24f,5.15f})*rotate_x(std::atan2(.58f,1.5f))*scale({3.8f,.1f,1.61f});
    objects_.push_back({ShowcaseGeometry::Cube,ramp,steel});
    ground_.push_back({center+V3{1.7f,0,4.4f},center+V3{5.5f,0,5.9f},.58f,0});
    railing(center+V3{1.7f,.58f,-4.7f},center+V3{1.7f,.58f,4.1f});
    for(float z:{-3.f,2.f}){
        object(ShowcaseGeometry::Cube,center+V3{-4.5f,-.5f,z},{1.9f,1,1.9f},concrete({.15f,.22f,.23f,1}));
        object(ShowcaseGeometry::Cylinder,center+V3{-4.5f,1.1f,z},{1.6f,2.2f,1.6f},painted({.13f,.27f,.24f,1}),true);
        object(ShowcaseGeometry::Sphere,center+V3{-4.5f,2.2f,z},{1.6f,.45f,1.6f},metal());
        pipe(center+V3{-4.5f,2.3f,z},center+V3{-4.5f,4.7f,z},.16f);
    }
    pipe(center+V3{-5,4.7f,-4},center+V3{-5,4.7f,4},.18f);pipe(center+V3{-5,4.7f,0},center+V3{-1.8f,4.7f,0},.18f);
    pipe(center+V3{-1.8f,4.7f,0},center+V3{-1.8f,3.3f,0},.18f);
    object(ShowcaseGeometry::Pipe,center+V3{-6,1.4f,-3},{.65f,2.8f,.65f},painted({.23f,.3f,.26f,1}),true);
    station(center+V3{4.75f,.58f,-2.2f},Action::Water);
    light(center+V3{-2,5.4f,1},{.17f,.7f,.85f},20,13,true,{0,-1,0},.63f);
    fixture_objects_.push_back(objects_.size());object(ShowcaseGeometry::Cube,center+V3{-2,5.7f,1},{2.6f,.12f,.3f},glow({1,4,5}));
    light(center+V3{4.5f,3.8f,-4},{1,.6f,.23f},9,9);
    emitters_.push_back({ShowcaseParticleKind::Spray,center+V3{-1.8f,3.3f,0},{.11f,0,.11f},{0,-2,0},140});
    emitters_.push_back({ShowcaseParticleKind::Steam,center+V3{-4.5f,2.4f,-3},{.09f,.04f,.09f},{.4f,.6f,0},8});
}
void ShowcaseWorld::boiler_room(V3 center){
    for(float x:{-4.4f,4.4f})if(scene_!=Scene::Facility||x<0)volumes_.push_back({uint32_t(volumes_.size()+1),ShowcaseVolumeAsset::SimpleFire,inverse(translate(center+V3{x,1.1f,.4f})*scale({1.5f,2.2f,1.2f})),3,5,x});
    volumes_.push_back({uint32_t(volumes_.size()+1),ShowcaseVolumeAsset::ChimneySmoke,inverse(translate(center+V3{-4.2f,3.6f,.2f})*scale({2.7f,4.1f,2.3f})),2.4f,0,1.3f});
    room(center,{14,6,12});
    for(float x:{-4.4f,4.4f}){
        object(ShowcaseGeometry::Cylinder,center+V3{x,1.9f,-2},{3,3.8f,3},painted({.23f,.17f,.13f,1}),true);
        object(ShowcaseGeometry::Sphere,center+V3{x,3.85f,-2},{3,.7f,3},metal({.23f,.21f,.17f,1}));
        pipe(center+V3{x,4,-2},center+V3{x,5.7f,-2},.45f);
        object(ShowcaseGeometry::Cube,center+V3{x,.7f,-.45f},{1.65f,1.4f,.18f},metal({.06f,.05f,.04f,1}));
        fire_objects_.push_back(objects_.size());object(ShowcaseGeometry::Cube,center+V3{x,.75f,-.34f},{1.27f,1.02f,.035f},glow({3.5f,.42f,.04f}));
        for(int n=0;n<6;++n)object(ShowcaseGeometry::Cube,center+V3{x-.65f+float(n)*.26f,.75f,-.25f},{.055f,1.15f,.07f},metal({.035f,.028f,.02f,1}));
        light(center+V3{x,1.2f,.2f},{1,.22f,.045f},18,10);
        emitters_.push_back({ShowcaseParticleKind::Fire,center+V3{x,.28f,.35f},{1.1f,.1f,.45f},{0,1.2f,0},85});
        emitters_.push_back({ShowcaseParticleKind::Smoke,center+V3{x,1.1f,.25f},{.75f,.15f,.5f},{.1f,.8f,0},21});
        emitters_.push_back({ShowcaseParticleKind::Ember,center+V3{x,.6f,.35f},{1,.15f,.4f},{0,1.8f,0},18});
        emitters_.push_back({ShowcaseParticleKind::Spark,center+V3{x,1,.35f},{.8f,.1f,.3f},{0,1.9f,.35f},10});
        emitters_.push_back({ShowcaseParticleKind::Steam,center+V3{x,.4f,.3f},{1.2f,.1f,.5f},{0,1,0},35});
        emitters_.push_back({ShowcaseParticleKind::Spray,center+V3{x,4.8f,.3f},{.8f,0,.5f},{0,-3,0},100});
        object(ShowcaseGeometry::Cylinder,center+V3{x,4.85f,.3f},{.22f,.12f,.22f},metal());
        pipe(center+V3{x,5.1f,-3},center+V3{x,5.1f,3},.08f);
    }
    pipe(center+V3{-5.5f,4.5f,-4.5f},center+V3{5.5f,4.5f,-4.5f},.22f);
    for(float x:{-1.8f,1.8f})object(ShowcaseGeometry::Barrel,center+V3{x,.6f,-4.4f},{.85f,1.2f,.85f},painted({.46f,.2f,.06f,1}),true);
    station(center+V3{2.1f,0,3.1f},Action::Sprinklers);
    fixture_objects_.push_back(objects_.size());object(ShowcaseGeometry::Cube,center+V3{0,5.7f,2.5f},{3,.1f,.25f},glow({1.4f,2.7f,3.5f}));
    light(center+V3{0,5.2f,2.5f},{.42f,.65f,1},14,13,true,{0,-1,-.25f},.63f);
}
void ShowcaseWorld::lighting_room(){
    room({0,0,0},{14,7,12},true);
    const auto steel=metal({.12f,.17f,.19f,1},.32f),orange=painted({.62f,.24f,.055f,1});
    // Three adjacent receivers distinguish a dry floor, projector wetness and
    // independently authored UV wetness while keeping their base material equal.
    for(int n=0;n<3;++n){auto material=concrete({.3f,.32f,.3f,1});material.wetness=0;if(n==2){material.mask.texture=SurfaceMask::Porous;material.mask.wetness=.95f;material.mask.roughness=-.45f;material.mask.uv={2.1f,1.4f,.13f,.21f};}object(ShowcaseGeometry::Cube,{-3.6f+float(n)*3.6f,.01f,2.6f},{3.45f,.04f,3.9f},material);}
    for(float z:{-4.5f,-.5f}){
        for(float x:{-5.5f,5.5f}){object(ShowcaseGeometry::Cube,{x,1.65f,z},{1.5f,3.3f,1.35f},painted({.19f,.26f,.27f,1}),true);for(int n=0;n<9;++n)object(ShowcaseGeometry::Cube,{x,1+float(n)*.2f,z+.69f},{1.2f,.06f,.025f},steel);object(ShowcaseGeometry::Cube,{x,2.85f,z+.7f},{.55f,.22f,.02f},glow({.04f,.4f,.19f}));}
        pipe({-6.1f,5.7f,z},{6.1f,5.7f,z},.18f);for(float x:{-4.f,0.f,4.f})object(ShowcaseGeometry::Cube,{x,6.2f,z},{.08f,1.05f,.08f},steel);
    }
    for(float x:{-6.3f,6.3f}){pipe({x,.5f,-5.5f},{x,5.9f,-5.5f},.23f);pipe({x,5.9f,-5.5f},{x,5.9f,5.5f},.23f);for(float z:{-3.f,0.f,3.f}){object(ShowcaseGeometry::Cylinder,{x*.95f,1.1f,z},{.9f,2.2f,.9f},orange,true);object(ShowcaseGeometry::Sphere,{x*.95f,2.2f,z},{.9f,.25f,.9f},steel);}}
    object(ShowcaseGeometry::Cube,{0,.48f,-4.45f},{5.9f,.18f,1.8f},steel);
    for(float x:{-2.4f,2.4f})object(ShowcaseGeometry::Cube,{x,.22f,-4.45f},{.2f,.5f,1.65f},steel);
    for(float x:{-2.f,0.f,2.f})object(ShowcaseGeometry::Barrel,{x,1.25f,-4.5f},{.9f,1.3f,.9f},orange,true);
    for(float x:{-3.5f,3.5f}){object(ShowcaseGeometry::Cube,{x,3.3f,-2.1f},{.16f,6.6f,.16f},steel);object(ShowcaseGeometry::Cube,{x,6.45f,0},{.17f,.2f,11.5f},steel);}
    for(int n=0;n<12;++n)object(ShowcaseGeometry::Cube,{-2.6f+float(n)*.47f,6.45f,-2.1f},{.07f,.12f,2.2f},steel);
    for(float z:{-1.f,1.f})object(ShowcaseGeometry::PowerBox,{-4.7f,.75f,z},{.8f,1.5f,.65f},painted({.16f,.23f,.26f,1}),true);
    station({4.6f,0,3.8f},Action::Lights);
    ShowcaseMaterial glass;glass.surface=ShowcaseSurface::Glass;glass.roughness=.035f;glass.tint={.7f,.91f,.95f};glass.thickness=.03f;
    glass_objects_.push_back(objects_.size());objects_.push_back({ShowcaseGeometry::Plane,translate({-2.2f,1.8f,-1.1f})*rotate_x(pi/2)*scale({2.4f,1,3.1f}),glass,true});
    for(float x:{-3.45f,-.95f})object(ShowcaseGeometry::Cube,{x,1.8f,-1.1f},{.085f,3.35f,.12f},steel,true);
    for(float y:{.15f,3.45f})object(ShowcaseGeometry::Cube,{-2.2f,y,-1.1f},{2.6f,.1f,.12f},steel);
    blockers_.push_back({{-3.4f,.2f,-1.13f},{-1,3.4f,-1.07f}});
    auto add_fixture=[&](LightType type,LightMobility mobility,V3 position,V3 color,float lumens,float width,float height,V3 direction){
        const size_t index=lights_.size();light(position,color,lumens,14,true,direction,type==LightType::Spot?.72f:-1);
        auto&l=lights_.back();l.type=type;l.mobility=mobility==LightMobility::Static&&scene_==Scene::LightingDynamic?LightMobility::Stationary:mobility;l.units=LightIntensityUnit::Lumen;l.width=width;l.height=height;l.inner_cone_cosine=.91f;l.source_radius=.08f;l.right={1,0,0};
        auto fixture=glow(color*3);const size_t object_index=objects_.size();object(ShowcaseGeometry::Cube,position+V3{0,.1f,0},{type==LightType::Rect?width:.22f,.12f,type==LightType::Rect?height:.22f},fixture);
        objects_.back().movable=mobility==LightMobility::Movable;animated_fixtures_.push_back({index,object_index,objects_.back().transform});
    };
    add_fixture(LightType::Point,LightMobility::Static,{-3.2f,3.7f,-3.3f},{1,.65f,.28f},1250,1,1,{0,-1,0});
    add_fixture(LightType::Rect,LightMobility::Static,{2.7f,5.3f,-3.1f},{.48f,.73f,1},2600,2.2f,.55f,{0,-1,0});
    add_fixture(LightType::Spot,LightMobility::Stationary,{-3.3f,5.35f,.5f},{.54f,1,.73f},1400,1,1,{.12f,-1,-.12f});
    add_fixture(LightType::Rect,LightMobility::Stationary,{3.3f,4.8f,2.5f},{1,.36f,.12f},1700,1.8f,.4f,{-.1f,-1,-.15f});
    add_fixture(LightType::Point,LightMobility::Movable,{0,2.8f,-1.3f},{.46f,.63f,1},650,1,1,{0,-1,0});
    add_fixture(LightType::Spot,LightMobility::Movable,{-1.7f,4.6f,2.3f},{1,.6f,.27f},800,1,1,{0,-1,0});
    add_fixture(LightType::Rect,LightMobility::Movable,{1.7f,4.5f,.1f},{.65f,.82f,1},900,1.3f,.42f,{0,-1,0});
    light({0,10,0},{.53f,.64f,1},.35f,100,false,unit(V3{.3f,-1,.2f}));lights_.back().type=LightType::Directional;lights_.back().units=LightIntensityUnit::Lux;lights_.back().casts_shadow=true;
    light({0,8,0},{.18f,.24f,.36f},.12f,100);lights_.back().type=LightType::Sky;lights_.back().units=LightIntensityUnit::Luminance;
    for(float z:{-1.7f,1.5f}){object(ShowcaseGeometry::Cube,{.1f,1.45f,z},{1.3f,2.9f,.18f},painted({.36f,.24f,.1f,1}));objects_.back().movable=true;moving_occluders_.push_back({objects_.size()-1,objects_.back().transform});}
    auto decal=[&](V3 center,V3 dimensions,SurfaceMask texture,float wetness,uint32_t layer){ShowcaseDecal d;d.id=uint32_t(decals_.size()+1);d.layer=layer;d.world_to_decal=inverse(translate(center)*scale(dimensions));d.texture=texture;d.wetness=wetness;d.roughness=-.4f;d.normal=.12f;d.color={.16f,.19f,.14f,.35f};decals_.push_back(d);};
    decal({0,.04f,2.6f},{3.4f,.16f,3.8f},SurfaceMask::Leak,.9f,0);
    decal({.4f,.045f,2.2f},{1.1f,.17f,2.7f},SurfaceMask::Footprints,.85f,1);
    decal({-4.8f,.015f,-3.1f},{2.2f,.16f,2.4f},SurfaceMask::Grime,.18f,0);
    decal({3.9f,.015f,-3.8f},{1.6f,.16f,2.7f},SurfaceMask::Leak,.95f,0);
    emitters_.push_back({ShowcaseParticleKind::Dust,{0,3,0},{10,4,9},{.03f,.02f,0},18});
}
void ShowcaseWorld::build(){
    switch(scene_){
    case Scene::Facility:
        yard({0,0,11});glass_room({0,0,-2},true);water_room({0,0,-13});boiler_room({0,0,-25});
        break;
    case Scene::GlassLab:glass_room({0,0,0},true);break;
    case Scene::Waterworks:water_room({0,0,0});break;
    case Scene::BoilerRoom:boiler_room({0,0,0});break;
    case Scene::LightingBaked:case Scene::LightingDynamic:lighting_room();break;
    }
    for(size_t i=0;i<objects_.size();++i)objects_[i].id=i+1;
    for(size_t index:moving_lights_)lights_[index].mobility=LightMobility::Movable;
    for(auto&e:emitters_)for(const auto&volume:volumes_){bool fire=e.kind==ShowcaseParticleKind::Fire,vapor=e.kind==ShowcaseParticleKind::Smoke||e.kind==ShowcaseParticleKind::Steam;if((!fire&&!vapor)||(fire!=(volume.asset!=ShowcaseVolumeAsset::ChimneySmoke)))continue;const auto&m=volume.world_to_volume;V3 p{m.v[0]*e.position.x+m.v[4]*e.position.y+m.v[8]*e.position.z+m.v[12],m.v[1]*e.position.x+m.v[5]*e.position.y+m.v[9]*e.position.z+m.v[13],m.v[2]*e.position.x+m.v[6]*e.position.y+m.v[10]*e.position.z+m.v[14]};if(std::abs(p.x)<=.5f&&p.y>=-.9f&&p.y<=.5f&&std::abs(p.z)<=.5f)e.hero_volume=true;}

}

void ShowcaseWorld::reset(){
    const auto seed=state_.seed;state_={};state_.seed=seed;
    random_=seed?seed:0x6d2b79f5u;accumulator_=0;particles_.clear();render_particles_.clear();
    for(auto&e:emitters_)e.carry=0;
    camera_view();animate();previous_eye_=eye_;previous_target_=target_;lighting_.reset_dynamic();lighting_frame_=0;
    if(has_water_)water_.reset(water_center_,{6.4f,8});
}
bool ShowcaseWorld::load_geometry(const char*root,bool bake_static){
    if(!lighting_scene())return true;if(!lighting_.load_geometry(root))return false;
    lighting_.initialize(objects_,lights_,{{-5.25f,.65f,-4.8f},{1.5f,1.8f,2.4f},{8,4,5}},scene_==Scene::LightingBaked&&bake_static);return true;
}
bool ShowcaseWorld::load_bake(const char*path){
    if(scene_!=Scene::LightingBaked||!lighting_.read_bake(path,objects_,lights_))return false;
    auto atlas=std::filesystem::path(path);atlas.replace_extension(".kglmap");return lighting_.read_lightmap(atlas.string().c_str());
}
void ShowcaseWorld::match_view(const ShowcaseWorld&other){
    state_=other.state_;random_=other.random_;particles_=other.particles_;render_particles_.clear();for(size_t i=0;i<std::min(emitters_.size(),other.emitters_.size());++i)emitters_[i].carry=other.emitters_[i].carry;accumulator_=other.accumulator_;yaw_=other.yaw_;pitch_=other.pitch_;inspection_distance_=other.inspection_distance_;eye_=other.eye_;target_=other.target_;previous_eye_=other.previous_eye_;previous_target_=other.previous_target_;inspection_target_=other.inspection_target_;walking_eye_=other.walking_eye_;view_=other.view_;sequence_=other.sequence_;animate();
}
void ShowcaseWorld::set_view(std::string_view view){
    if(view=="overview")view_=View::Overview;else if(view=="glass")view_=View::Glass;else if(view=="water")view_=View::Water;else if(view=="fire")view_=View::Fire;else throw std::invalid_argument("Unknown showcase view");
    camera_view();previous_eye_=eye_;previous_target_=target_;
}
void ShowcaseWorld::set_sequence(std::string_view sequence){
    if(sequence=="idle")sequence_=Sequence::Idle;else if(sequence=="interact")sequence_=Sequence::Interact;else if(sequence=="tour")sequence_=Sequence::Tour;else throw std::invalid_argument("Unknown showcase sequence");
    reset();
}
void ShowcaseWorld::set_particle_limit(uint32_t limit){particle_limit_=std::min(limit,max_particles);if(particles_.size()>particle_limit_)particles_.resize(particle_limit_);state_.particle_count=particles_.size();}
void ShowcaseWorld::camera_view(){
    state_.inspection=view_!=View::Overview;
    V3 aim;
    switch(scene_){
    case Scene::Facility:eye_={.7f,eye_height,9.1f};aim={-.45f,1.95f,-3.5f};break;
    case Scene::GlassLab:eye_={3.5f,eye_height,4.3f};aim={-1.2f,1.8f,-1.5f};break;
    case Scene::Waterworks:eye_={3.6f,eye_height,5.55f};aim={-2.2f,1.15f,-1.4f};break;
    case Scene::BoilerRoom:eye_={0,eye_height,5};aim={-2.4f,1.8f,-1};break;
    case Scene::LightingBaked:case Scene::LightingDynamic:eye_={4.5f,eye_height,5.4f};aim={-1.3f,2.1f,-1.5f};break;
    }
    eye_.y=eye_height+ground_height(eye_);walking_eye_=eye_;
    if(view_!=View::Overview){
        V3 center{};
        if(scene_==Scene::Facility)center.z=view_==View::Glass?-2:view_==View::Water?-13:-25;
        if(view_==View::Glass){eye_=center+V3{4.9f,2.4f,4.1f};aim=center+V3{-.6f,1.8f,-1.8f};}
        else if(view_==View::Water){eye_=center+V3{5.2f,3.6f,4.8f};aim=center+V3{-2.35f,.65f,-.6f};}
        else {eye_=center+V3{.3f,2.2f,4.9f};aim=center+V3{-2.3f,1.7f,-.6f};}
    }
    if(!state_.inspection)eye_.y=eye_height+ground_height(eye_);
    target_=aim;V3 direction=unit(aim-eye_);yaw_=std::atan2(direction.x,-direction.z);pitch_=std::asin(std::clamp(direction.y,-1.f,1.f));
    inspection_target_=target_;inspection_distance_=std::sqrt(square_distance(eye_,target_));
}
void ShowcaseWorld::update_camera(){
    V3 direction{std::sin(yaw_)*std::cos(pitch_),std::sin(pitch_),-std::cos(yaw_)*std::cos(pitch_)};
    if(state_.inspection){target_=inspection_target_;eye_=target_-direction*inspection_distance_;}else target_=eye_+direction*8;
}
int ShowcaseWorld::nearest_station() const{
    int found=-1;float nearest=state_.inspection?100000.f:9.f;
    for(size_t n=0;n<stations_.size();++n){float distance=square_distance(eye_,stations_[n].position);if(distance<nearest){nearest=distance;found=int(n);}}
    return found;
}
std::string_view ShowcaseWorld::interaction_prompt() const{
    int station=nearest_station();if(station<0)return "FOLLOW THE MARKED ROUTE TO A CONTROL STATION";
    switch(stations_[size_t(station)].action){
    case Action::Lights:return state_.lights_on?"SWITCH WORK LIGHTS OFF":"SWITCH WORK LIGHTS ON";
    case Action::Water:return state_.water_on?"CLOSE VALVE / MAKE A SPLASH":"OPEN VALVE / MAKE A SPLASH";
    case Action::Sprinklers:return state_.sprinklers_on?"STOP SPRINKLERS":"ACTIVATE SPRINKLERS";
    case Action::Glass:return "CYCLE CLEAR / TINTED / FROSTED GLASS";
    }return {};
}
void ShowcaseWorld::activate(Action action){
    switch(action){case Action::Lights:state_.lights_on=!state_.lights_on;break;case Action::Water:state_.water_on=!state_.water_on;splash();break;case Action::Sprinklers:state_.sprinklers_on=!state_.sprinklers_on;break;case Action::Glass:state_.glass_variant=(state_.glass_variant+1)%3;break;}
}
void ShowcaseWorld::input(const WorldInput& i){
    if(i.reset)reset();
    if(i.toggle_animation)state_.animation_frozen=!state_.animation_frozen;
    if(i.cycle_light_color)state_.light_color_variant=(state_.light_color_variant+1)%3;
    state_.stationary_intensity=std::clamp(state_.stationary_intensity+i.light_intensity_delta,.05f,4.f);
    if(i.toggle_inspection){
        state_.inspection=!state_.inspection;
        if(state_.inspection){walking_eye_=eye_;inspection_target_=target_;inspection_distance_=std::sqrt(square_distance(eye_,target_));}
        // Inspection may orbit through walls; walking resumes at its last valid
        // capsule position rather than trapping the player inside solid scenery.
        else eye_=walking_eye_;
    }
    yaw_+=i.look_x;pitch_=std::clamp(pitch_+i.look_y,-1.35f,1.35f);
    if(state_.inspection)inspection_distance_=std::clamp(inspection_distance_*std::exp(-i.zoom*.1f),.6f,45.f);
    if(i.interact){int station=nearest_station();if(station>=0)activate(stations_[size_t(station)].action);}
    update_camera();
}
float ShowcaseWorld::ground_height(V3 p) const{
    float height=0;
    for(const auto&g:ground_)if(p.x>=g.lo.x&&p.x<=g.hi.x&&p.z>=g.lo.z&&p.z<=g.hi.z){float t=std::clamp((p.z-g.lo.z)/(g.hi.z-g.lo.z),0.f,1.f);height=std::max(height,g.low_height+(g.high_height-g.low_height)*t);}
    return height;
}
void ShowcaseWorld::move(V3 delta){
    // Split travel to avoid crossing thin walls during fast input or low FPS.
    int steps=std::max(1,int(std::ceil(std::sqrt(dot(delta,delta))/.12f)));delta=delta*(1.f/float(steps));
    for(int n=0;n<steps;++n)for(int axis=0;axis<2;++axis){
        V3 candidate=eye_;if(axis==0)candidate.x+=delta.x;else candidate.z+=delta.z;
        bool blocked=false;float foot=ground_height(candidate);
        for(const auto&b:blockers_){
            if(b.hi.y<foot+.15f||b.lo.y>foot+eye_height)continue;
            const float x=std::clamp(candidate.x,b.lo.x,b.hi.x),z=std::clamp(candidate.z,b.lo.z,b.hi.z);
            if((candidate.x-x)*(candidate.x-x)+(candidate.z-z)*(candidate.z-z)<body_radius*body_radius){blocked=true;break;}
        }
        if(!blocked)eye_=candidate;
    }
    eye_.x=std::clamp(eye_.x,-7.45f,7.45f);eye_.z=std::clamp(eye_.z,scene_==Scene::Facility?-30.7f:-5.7f,scene_==Scene::Facility?17.7f:5.7f);eye_.y=eye_height+ground_height(eye_);
}
void ShowcaseWorld::advance(double seconds,const WorldInput& i,bool paused){
    if(paused){accumulator_=0;return;}
    input(i);if(!std::isfinite(seconds)||seconds<0)return;
    accumulator_+=std::min(seconds,.25);unsigned count=0;
    while(accumulator_+1e-12>=tick_seconds&&count<8){step(i.move_forward,i.move_right,i.move_up,i.sprint);accumulator_=std::max(0.,accumulator_-tick_seconds);++count;}
    if(count==8&&accumulator_>=tick_seconds)accumulator_=std::fmod(accumulator_,tick_seconds);
}
void ShowcaseWorld::tick(const WorldInput& i){input(i);step(i.move_forward,i.move_right,i.move_up,i.sprint);}
float ShowcaseWorld::interpolation() const{return float(std::clamp(accumulator_/tick_seconds,0.,1.));}
void ShowcaseWorld::step(float forward,float right,float up,bool sprint){
    previous_eye_=eye_;previous_target_=target_;++state_.tick;if(!state_.animation_frozen)++state_.animation_tick;
    V3 direction{std::sin(yaw_),0,-std::cos(yaw_)},side{std::cos(yaw_),0,std::sin(yaw_)};
    V3 travel=direction*forward+side*right;float magnitude=std::sqrt(dot(travel,travel));if(magnitude>1)travel=travel*(1/magnitude);
    if(state_.inspection)inspection_target_=inspection_target_+(travel+V3{0,up,0})*(5*fixed_step);
    else move(travel*(3.2f*(sprint?1.8f:1.f)*fixed_step));
    if(sequence_==Sequence::Interact&&state_.tick==120){
        switch(scene_){case Scene::Facility:activate(Action::Lights);activate(Action::Glass);activate(Action::Water);activate(Action::Sprinklers);break;case Scene::GlassLab:activate(Action::Glass);break;case Scene::Waterworks:activate(Action::Water);break;case Scene::BoilerRoom:activate(Action::Sprinklers);break;case Scene::LightingBaked:case Scene::LightingDynamic:state_.light_color_variant=(state_.light_color_variant+1)%3;state_.stationary_intensity=.45f;break;}
    }
    if(sequence_==Sequence::Tour){
        if(scene_==Scene::Facility){
            // A repeating three-minute route follows the real traversable lane,
            // pausing at each feature without moving the gameplay simulation.
            constexpr V3 route[]={{0,1.7f,16},{0,1.7f,6},{0,1.7f,1},{5.8f,1.7f,0},{5.8f,1.7f,-4.3f},{0,1.7f,-5.6f},{0,1.7f,-7.7f},{3.6f,1.7f,-7.7f},{3.6f,2.28f,-9},{3.6f,2.28f,-18.5f},{0,1.7f,-18.5f},{0,1.7f,-20},{0,1.7f,-23.5f},{0,1.7f,-27}};
            float phase=std::fmod(float(state_.tick)/830.f,float(std::size(route)-1));size_t n=size_t(phase);float t=phase-float(n);t=t*t*(3-2*t);eye_=mix(route[n],route[n+1],t);eye_.y=eye_height+ground_height(eye_);
            V3 aim=route[n+1];if(square_distance(eye_,aim)<.1f)aim=aim+V3{-1,0,-2};V3 d=unit(aim-eye_);yaw_=std::atan2(d.x,-d.z);pitch_=-.04f;state_.inspection=false;
        }else {state_.inspection=true;yaw_+=fixed_step*.075f;}
        if(state_.tick%240==120){if(scene_==Scene::GlassLab)activate(Action::Glass);else if(scene_==Scene::Waterworks)activate(Action::Water);else if(scene_==Scene::BoilerRoom)activate(Action::Sprinklers);}
    }
    update_camera();animate();if(!state_.animation_frozen){emit_particles();if(has_water_)water_.step();}state_.particle_count=particles_.size();
}
void ShowcaseWorld::update_lighting(){if(lighting_scene())lighting_.update(objects_,lights_,++lighting_frame_);}
void ShowcaseWorld::animate(){
    if(lighting_scene()){
        const float time=float(state_.animation_tick)*fixed_step;
        constexpr V3 colors[]={{1,1,1},{.32f,.62f,1},{1,.3f,.12f}};
        for(size_t n=0;n<lights_.size();++n){auto&l=lights_[n];if(l.mobility==LightMobility::Static)continue;l.intensity=light_intensities_[n]*(state_.lights_on?state_.stationary_intensity:.08f);V3 color=colors[state_.light_color_variant];l.color={light_colors_[n].x*color.x,light_colors_[n].y*color.y,light_colors_[n].z*color.z};}
        for(auto&fixture:animated_fixtures_){auto&l=lights_[fixture.light];auto&o=objects_[fixture.object];
            if(l.mobility==LightMobility::Movable){float phase=time*.55f+float(fixture.light);l.position=light_positions_[fixture.light]+V3{std::sin(phase)*1.15f,std::sin(phase*.7f)*.18f,std::cos(phase*.8f)*.55f};l.direction=unit(V3{std::sin(phase)*.5f,-1,std::cos(phase)*.35f});l.right=unit(cross(l.direction,{0,0,1}));l.transform_revision=uint32_t(state_.animation_tick+1);V3 up=unit(cross(l.right,l.direction));M4 orientation=identity();orientation.v[0]=l.right.x;orientation.v[1]=l.right.y;orientation.v[2]=l.right.z;orientation.v[4]=-l.direction.x;orientation.v[5]=-l.direction.y;orientation.v[6]=-l.direction.z;orientation.v[8]=up.x;orientation.v[9]=up.y;orientation.v[10]=up.z;o.transform=translate(l.position+l.direction*-.07f)*orientation*scale({l.type==LightType::Rect?l.width:.22f,.12f,l.type==LightType::Rect?l.height:.22f});o.geometry_revision=l.transform_revision;}
            o.material.emissive=light_color(l)*(l.intensity/light_intensities_[fixture.light]*3);
        }
        for(size_t n=0;n<moving_occluders_.size();++n){auto[index,base]=moving_occluders_[n];objects_[index].transform=translate({std::sin(time*.6f+float(n)*pi)*1.4f,0,0})*base*rotate_y(std::sin(time*.4f)*.4f);objects_[index].geometry_revision=uint32_t(state_.animation_tick+1);}
        return;
    }
    const float target=state_.sprinklers_on?.12f:1.f;if(!state_.animation_frozen)state_.fire_level+=(target-state_.fire_level)*.035f;
    for(size_t n=0;n<lights_.size();++n){auto&l=lights_[n];l.intensity=light_intensities_[n]*(state_.lights_on?1:.13f);if(l.color.x>.9f&&l.color.y<.3f)l.intensity*=state_.fire_level*(.92f+.08f*std::sin(float(state_.animation_tick)*.71f+float(n)));}
    for(size_t n:fixture_objects_)objects_[n].material.emissive=state_.lights_on?V3{2.4f,3.2f,4}:V3{.15f,.21f,.27f};
    for(auto&v:volumes_)if(v.asset!=ShowcaseVolumeAsset::ChimneySmoke){v.emission=25*state_.fire_level;v.density=3*state_.fire_level;}
    for(size_t n:fire_objects_)objects_[n].material.emissive=V3{3.5f,.42f,.04f}*state_.fire_level;
    for(size_t n:moving_lights_){float phase=float(state_.animation_tick)*fixed_step+float(n);lights_[n].transform_revision=uint32_t(state_.animation_tick+1);lights_[n].position=light_positions_[n]+V3{std::sin(phase*.7f)*.75f,std::sin(phase*.9f)*.25f,std::cos(phase*.5f)*.45f};}
    for(size_t n=0;n<glass_objects_.size();++n){auto&m=objects_[glass_objects_[n]].material;auto variant=(state_.glass_variant+uint32_t(n))%3;m.roughness=variant==2?.42f:.025f;m.tint=variant==1?V3{.23f,.72f,.55f}:V3{.84f,.96f,.98f};m.thickness=variant==1?.16f:.025f;m.distortion=variant==2?.035f:.009f;}
}
float ShowcaseWorld::random_unit(){random_^=random_<<13;random_^=random_>>17;random_^=random_<<5;return float(random_>>8)*(1.f/16777216.f);}
void ShowcaseWorld::spawn(const Emitter&e){
    ShowcaseParticle p;p.kind=e.kind;p.hero_volume=e.hero_volume;
    p.position=e.position+V3{(random_unit()-.5f)*e.extent.x,(random_unit()-.5f)*e.extent.y,(random_unit()-.5f)*e.extent.z};p.previous=p.position;
    p.velocity=e.velocity+V3{(random_unit()-.5f)*.7f,(random_unit()-.5f)*.5f,(random_unit()-.5f)*.7f};
    p.rotation=random_unit()*2*pi;p.lifetime=1.4f+random_unit()*1.4f;p.size=.1f+random_unit()*.1f;
    switch(p.kind){
    case ShowcaseParticleKind::Rain:p.color={.57f,.77f,.9f,.28f};p.size=.018f;p.lifetime=.7f;break;
    case ShowcaseParticleKind::Spray:p.color={.48f,.78f,.87f,.64f};p.size=.025f+random_unit()*.035f;p.lifetime=2;break;
    case ShowcaseParticleKind::Fire:p.color={5.5f,1.1f,.08f,.65f};p.size=.27f+random_unit()*.3f;p.lifetime=.6f+random_unit()*.8f;p.velocity.y*=.8f+random_unit()*.8f;break;
    case ShowcaseParticleKind::Smoke:p.color={.15f,.18f,.2f,.22f};p.size=.35f+random_unit()*.45f;p.lifetime=3+random_unit()*3;break;
    case ShowcaseParticleKind::Steam:p.color={.64f,.77f,.8f,.19f};p.size=.24f+random_unit()*.3f;p.lifetime=1.5f+random_unit()*2;break;
    case ShowcaseParticleKind::Spark:p.color={7,3,.35f,.95f};p.size=.02f;p.lifetime=.25f+random_unit()*.65f;p.velocity.x*=4;p.velocity.z*=4;p.velocity.y*=1.6f;break;
    case ShowcaseParticleKind::Ember:p.color={4,.38f,.025f,.85f};p.size=.016f+random_unit()*.025f;p.lifetime=1+random_unit()*2;break;
    case ShowcaseParticleKind::Dust:p.color={.54f,.62f,.69f,.2f};p.size=.013f+random_unit()*.017f;p.lifetime=5+random_unit()*4;p.velocity=e.velocity;break;
    case ShowcaseParticleKind::Ripple:p.color={.31f,.64f,.68f,.28f};p.size=.16f;p.lifetime=.8f+random_unit()*.6f;p.velocity={};break;
    }
    if(particles_.size()<particle_limit_)particles_.push_back(p);
}
void ShowcaseWorld::splash(){
    if(!has_water_)return;
    Emitter e{ShowcaseParticleKind::Spray,water_center_+V3{.5f,.15f,.5f},{.6f,.2f,.6f},{0,3.4f,0},0};
    for(int n=0;n<96;++n){size_t count=particles_.size();spawn(e);if(particles_.size()>count){particles_.back().velocity.x*=5;particles_.back().velocity.z*=5;}}
    water_.impact(e.position,1.8f,.48f);
    Emitter ripple{ShowcaseParticleKind::Ripple,e.position,{1,0,1},{},0};for(int n=0;n<8;++n)spawn(ripple);
}
void ShowcaseWorld::emit_particles(){
    for(size_t n=0;n<particles_.size();){
        auto&p=particles_[n];p.previous=p.position;p.age+=fixed_step;
        if(p.age>=p.lifetime||p.position.y<-.08f){p=particles_.back();particles_.pop_back();continue;}
        if(p.kind==ShowcaseParticleKind::Spray||p.kind==ShowcaseParticleKind::Spark)p.velocity.y-=9.8f*fixed_step;
        if(p.kind==ShowcaseParticleKind::Smoke||p.kind==ShowcaseParticleKind::Steam){p.velocity.x+=std::sin(p.age*1.7f+p.rotation)*.08f*fixed_step;p.size+=.12f*fixed_step;}
        if(p.kind==ShowcaseParticleKind::Ripple)p.size+=1.2f*fixed_step;
        p.position=p.position+p.velocity*fixed_step;
        if(has_water_&&(p.kind==ShowcaseParticleKind::Rain||p.kind==ShowcaseParticleKind::Spray)&&water_.contains(p.position)&&p.previous.y>water_.sample(p.previous).height&&p.position.y<=water_.sample(p.position).height){water_.impact(p.position,-std::clamp(std::abs(p.velocity.y)*.09f,.08f,.9f),std::max(.09f,p.size*3));p=particles_.back();particles_.pop_back();continue;}
        ++n;
    }
    for(auto&e:emitters_){
        float factor=1;
        const bool boiler=e.position.z<(scene_==Scene::Facility?-19.f:10.f)&&(scene_==Scene::Facility||scene_==Scene::BoilerRoom);
        if(e.kind==ShowcaseParticleKind::Fire||e.kind==ShowcaseParticleKind::Ember||e.kind==ShowcaseParticleKind::Spark)factor=boiler?state_.fire_level:1;
        if(e.kind==ShowcaseParticleKind::Smoke&&boiler)factor=state_.sprinklers_on?.6f:1.f;
        if(e.kind==ShowcaseParticleKind::Steam&&boiler)factor=state_.sprinklers_on?1.f:0.f;
        if(e.kind==ShowcaseParticleKind::Spray)factor=boiler?(state_.sprinklers_on?1.f:0.f):(state_.water_on?1.f:0.f);
        if(e.kind==ShowcaseParticleKind::Ripple)factor=state_.water_on?1:.25f;
        e.carry+=e.rate*factor*fixed_step;
        while(e.carry>=1){e.carry-=1;spawn(e);}
    }
}
ShowcaseFrame ShowcaseWorld::frame(float interpolation){
    float t=std::clamp(interpolation,0.f,1.f);render_particles_.clear();
    for(const auto&particle:particles_){if(particle.hero_volume)continue;render_particles_.push_back(particle);render_particles_.back().position=mix(particle.previous,particle.position,t);}
    ShowcaseFrame result;result.objects=objects_;result.lights=lights_;result.particles=render_particles_;result.eye=mix(previous_eye_,eye_,t);result.target=mix(previous_target_,target_,t);result.time=(float(state_.animation_tick)+(state_.animation_tick?t-1:0))*fixed_step;result.water_height=water_center_.y;result.has_water=has_water_;
    if(lighting_scene()){result.irradiance_probes=lighting_.probes();result.baked_surfaces=lighting_.surfaces();result.baked_uv=lighting_.lightmap_uv();result.probe_grid=lighting_.grid();result.bake=lighting_.metadata();result.lighting=&lighting_;result.geometry_revision=state_.animation_tick+1;}
    streams_.clear();if(has_water_&&state_.water_on)for(const auto&e:emitters_)if(e.kind==ShowcaseParticleKind::Spray&&water_.contains(e.position)&&streams_.size()<max_water_streams)streams_.push_back({e.position,e.velocity,.045f,1.5f,water_.sample(e.position,t).height});
    result.streams=streams_;result.decals=decals_;result.volumes=volumes_;result.water=has_water_?&water_:nullptr;result.water_interpolation=t;return result;
}
}
