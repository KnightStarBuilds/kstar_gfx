#pragma once
#include "math.hpp"
#include "lighting_cpu.hpp"
#include "water_surface.hpp"
#include "surface_details.hpp"
#include "stream_geometry.hpp"
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace demo {
// The authored world is independent of either graphics backend. All geometry
// uses metres, right-handed Y-up coordinates, and unit primitive dimensions.
enum class ShowcaseGeometry : uint8_t { Cube, Plane, Sphere, Cylinder, Pipe, PowerBox, Barrel };
enum class ShowcaseTexture : uint8_t { Concrete, Metal, Tile, None };
enum class ShowcaseSurface : uint8_t { Opaque, Glass, Water };
enum class ShowcaseParticleKind : uint8_t { Rain, Spray, Fire, Smoke, Steam, Spark, Ember, Dust, Ripple };
struct ShowcaseMaterial {
    V4 base{1,1,1,1};
    V3 emissive{};
    float metallic=0,roughness=.65f,normal_scale=1,wetness=0;
    ShowcaseTexture texture=ShowcaseTexture::None;
    ShowcaseSurface surface=ShowcaseSurface::Opaque;
    float ior=1.5f,thickness=.012f,distortion=.018f,foam=1;
    V3 tint{1,1,1};
    MaterialSurfaceMask mask{};
};
struct ShowcaseObject {
    ShowcaseGeometry geometry=ShowcaseGeometry::Cube;
    M4 transform=identity();
    ShowcaseMaterial material{};
    bool double_sided=false;
    uint64_t id=0;
    uint32_t geometry_revision=1;
    bool movable=false;
    uint32_t decal_receiver_mask=1;
};
struct ShowcaseLight {
    V3 position{};float radius=10;
    V3 color{1,1,1};float intensity=1;
    V3 direction{0,-1,0};float spot_cosine=-1;
    bool casts_shadow=false;
    LightType type=LightType::Point;
    LightMobility mobility=LightMobility::Stationary;
    LightIntensityUnit units=LightIntensityUnit::Legacy;
    uint64_t id=0;
    uint32_t transform_revision=1;
    V3 right{1,0,0};
    float inner_cone_cosine=.9f,width=1,height=1,source_radius=.05f;
    float temperature=6500,tint=0,shadow_bias=.004f,shadow_softness=1,volumetric=1;
    bool use_temperature=false;
};
enum class ShowcaseVolumeAsset : uint8_t {SmallCampfire,SimpleFire,ChimneySmoke};
struct ShowcaseVolume {
    uint32_t id=0;ShowcaseVolumeAsset asset=ShowcaseVolumeAsset::SmallCampfire;
    M4 world_to_volume=identity();
    float density=3,emission=5,time_offset=0,playback_rate=1;
    V3 tint{1,1,1};
};
struct ShowcaseParticle {
    V3 position{},previous{},velocity{};
    V4 color{1,1,1,1};
    float size=.1f,rotation=0,age=0,lifetime=1;
    ShowcaseParticleKind kind=ShowcaseParticleKind::Smoke;
    bool hero_volume=false;
};
struct ShowcaseFrame {
    std::span<const ShowcaseObject> objects;
    std::span<const ShowcaseLight> lights;
    std::span<const ShowcaseParticle> particles;
    V3 eye{},target{},up{0,1,0};
    float time=0,water_height=.035f;
    bool has_water=false;
    std::span<const IrradianceProbe> irradiance_probes;
    std::span<const BakedSurface> baked_surfaces;
    std::span<const BakedUv> baked_uv;
    ProbeGrid probe_grid{};
    BakeMetadata bake{};
    const LightingCpu* lighting=nullptr;
    uint64_t geometry_revision=1;
    std::span<const ShowcaseDecal> decals;
    std::span<const ShowcaseVolume> volumes;
    std::span<const WaterStream> streams;
    const WaterSurface* water=nullptr;
    float water_interpolation=1;
};
struct WorldInput {
    float move_forward=0,move_right=0,move_up=0;
    // Look deltas are radians, applied once per input sample, not per substep.
    float look_x=0,look_y=0,zoom=0;
    bool interact=false,toggle_inspection=false,reset=false,sprint=false;
    bool toggle_animation=false,cycle_light_color=false;
    float light_intensity_delta=0;
};
struct ShowcaseState {
    uint64_t tick=0;
    uint32_t seed=1,glass_variant=0;
    bool lights_on=true,water_on=true,sprinklers_on=false,inspection=false;
    float fire_level=1;
    size_t particle_count=0;
    bool animation_frozen=false;
    uint32_t light_color_variant=0;
    float stationary_intensity=1;
    uint64_t animation_tick=0;
};

class ShowcaseWorld {
public:
    static constexpr float fixed_step=1.0f/60.0f;
    static constexpr uint32_t max_particles=16384;
    explicit ShowcaseWorld(std::string_view scene,uint32_t seed=1,uint32_t particle_limit=max_particles,bool bake_static=true);
    static bool supports_scene(std::string_view scene);
    std::string_view scene() const;
    void reset();
    void set_view(std::string_view view);
    void match_view(const ShowcaseWorld& other);
    bool load_bake(const char* path);
    bool load_geometry(const char* cooked_root,bool bake_static=false);
    void set_sequence(std::string_view sequence);
    void set_particle_limit(uint32_t limit);
    // Real-time stepping drops excess elapsed time after eight simulation ticks.
    // Captures call tick() directly for exactly one deterministic 60 Hz step.
    void advance(double elapsed_seconds,const WorldInput& input={},bool paused=false);
    void tick(const WorldInput& input={});
    // Call once per presented frame, independently of fixed simulation ticks.
    void update_lighting();
    ShowcaseFrame frame(float interpolation=1);
    float interpolation() const;
    const ShowcaseState& state() const{return state_;}
    std::string_view interaction_prompt() const;
    std::string_view sequence() const;

private:
    enum class Scene : uint8_t { Facility, GlassLab, Waterworks, BoilerRoom, LightingBaked, LightingDynamic };
    enum class Sequence : uint8_t { Idle, Interact, Tour };
    enum class View : uint8_t { Overview, Glass, Water, Fire };
    enum class Action : uint8_t { Lights, Water, Sprinklers, Glass };
    struct Box {V3 lo,hi;};
    struct Station {V3 position;Action action;};
    struct GroundPatch {V3 lo,hi;float low_height,high_height;};
    struct Emitter {ShowcaseParticleKind kind;V3 position,extent,velocity;float rate;float carry=0;bool hero_volume=false;};
    Scene scene_=Scene::Facility;
    Sequence sequence_=Sequence::Idle;
    View view_=View::Overview;
    ShowcaseState state_{};
    uint32_t random_=1,particle_limit_=max_particles;
    double accumulator_=0;
    float yaw_=0,pitch_=0,inspection_distance_=12;
    V3 eye_{},target_{},previous_eye_{},previous_target_{},inspection_target_{},walking_eye_{};
    V3 water_center_{};
    bool has_water_=false;
    std::vector<ShowcaseObject> objects_;
    std::vector<ShowcaseLight> lights_;
    std::vector<float> light_intensities_;
    std::vector<V3> light_positions_;
    std::vector<Box> blockers_;
    std::vector<Station> stations_;
    std::vector<GroundPatch> ground_;
    std::vector<Emitter> emitters_;
    std::vector<ShowcaseParticle> particles_,render_particles_;
    std::vector<size_t> glass_objects_,fixture_objects_,fire_objects_,moving_lights_;
    struct AnimatedFixture {size_t light,object;M4 transform;};
    std::vector<AnimatedFixture> animated_fixtures_;
    std::vector<std::pair<size_t,M4>> moving_occluders_;
    std::vector<V3> light_colors_;
    LightingCpu lighting_;
    uint64_t lighting_frame_=0;
    WaterSurface water_;
    std::vector<ShowcaseDecal> decals_;
    std::vector<ShowcaseVolume> volumes_;
    std::vector<WaterStream> streams_;
    bool lighting_scene() const{return scene_==Scene::LightingBaked||scene_==Scene::LightingDynamic;}
    void lighting_room();
    void build();
    void room(V3 center,V3 dimensions,bool roof=false,bool basin=false);
    void pipe(V3 from,V3 to,float radius);
    void railing(V3 from,V3 to);
    void floor_marks(V3 center,float length);
    void station(V3 position,Action action);
    void glass_room(V3 center,bool enclosed);
    void water_room(V3 center);
    void boiler_room(V3 center);
    void yard(V3 center);
    void object(ShowcaseGeometry geometry,V3 center,V3 dimensions,const ShowcaseMaterial& material,bool collide=false,float angle=0);
    void light(V3 position,V3 color,float intensity,float radius,bool shadow=false,V3 direction={0,-1,0},float cone=-1);
    void input(const WorldInput& input);
    void step(float forward,float right,float up,bool sprint);
    void camera_view();
    void update_camera();
    void move(V3 delta);
    float ground_height(V3 position) const;
    void activate(Action action);
    void animate();
    void emit_particles();
    void spawn(const Emitter& emitter);
    void splash();
    float random_unit();
    int nearest_station() const;
};
}
