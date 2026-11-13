#pragma once
#include "math.hpp"
#include <array>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace demo {
struct ShowcaseLight;
struct ShowcaseObject;
enum class LightType : uint8_t { Directional, Point, Spot, Rect, Sky };
enum class LightMobility : uint8_t { Static, Stationary, Movable };
enum class LightIntensityUnit : uint8_t { Legacy, Candela, Lumen, Lux, Luminance };
float light_intensity_candela(const ShowcaseLight& light);
V3 light_color(const ShowcaseLight& light);
uint32_t light_shadow_faces(const ShowcaseLight& light);

struct IrradianceProbe {
    V3 position{},baked_direct{},baked_indirect{},dynamic_indirect{};
    // Six axial irradiance lobes and distance moments reduce wall leakage.
    std::array<V3,6> baked_lobes{},dynamic_lobes{};
    std::array<V2,6> distance_moments{};
    std::array<float,6> sky_visibility{};
    uint64_t last_update=0;
    uint32_t geometry_revision=0;
};
struct ProbeGrid {V3 origin{},spacing{1,1,1};std::array<uint32_t,3> counts{};};
struct BakedSurface {
    uint64_t object_id=0;
    std::array<V3,6> direct{},indirect{};
};
struct BakedUv {
    uint64_t object_id=0;
    std::array<V4,6> u{},v{};
};
struct BakeMetadata {
    uint32_t version=1,diffuse_bounces=3;
    uint64_t scene_hash=0;
    uint32_t static_light_count=0;
    bool valid=false;
};
struct LightingSample {V3 baked_direct{},baked_indirect{},dynamic_indirect{};};

// Portable tracer for authored primitives and normalized imported triangles.
// Unit tests can omit imported resources; published scenes load their exact meshes.
// Storage is created once, and all frame updates use bounded preallocated work.
class LightingCpu {
public:
    static constexpr uint32_t max_probes=192,probes_per_update=16,rays_per_probe=96;
    LightingCpu();
    ~LightingCpu();
    LightingCpu(const LightingCpu&)=delete;
    LightingCpu&operator=(const LightingCpu&)=delete;
    void initialize(std::span<const ShowcaseObject> objects,std::span<const ShowcaseLight> lights,
                    ProbeGrid grid,bool bake_static);
    bool load_geometry(const char* cooked_root);
    void reset_dynamic();
    void update(std::span<const ShowcaseObject> objects,std::span<const ShowcaseLight> lights,uint64_t frame);
    std::span<const IrradianceProbe> probes() const{return probes_;}
    std::span<const BakedSurface> surfaces() const{return surfaces_;}
    std::span<const BakedUv> lightmap_uv() const{return lightmap_uv_;}
    LightingSample bake_irradiance(V3 position,V3 normal,std::span<const ShowcaseLight> lights,uint32_t seed,uint32_t samples=4) const;
    bool read_lightmap(const char* path);
    const ProbeGrid& grid() const{return grid_;}
    const BakeMetadata& metadata() const{return metadata_;}
    LightingSample sample(V3 position,V3 normal,uint64_t object_id=0) const;
    bool occluded(V3 from,V3 to,bool include_moving=true) const;
    bool write_bake(const char* path) const;
    bool read_bake(const char* path,std::span<const ShowcaseObject> objects,std::span<const ShowcaseLight> lights);
    static uint64_t scene_hash(std::span<const ShowcaseObject> objects,std::span<const ShowcaseLight> lights);
private:
    struct Workers;
    std::unique_ptr<Workers> workers_;
    void update_probe(uint32_t index,std::span<const ShowcaseLight> lights,uint64_t frame);
    struct Shape {M4 transform{},inverse{};V3 lo{},hi{},albedo{};uint64_t id=0;uint32_t revision=0;uint8_t kind=0;bool moving=false;};
    struct Node {V3 lo{},hi{};uint32_t first=0,count=0,left=0,right=0;};
    struct Hit {V3 position{},normal{},albedo{};float distance=1e30f;uint64_t object_id=0;bool found=false;};
    struct Triangle {V3 a{},b{},c{},normal{},lo{},hi{};};
    struct TriangleMesh {std::vector<Triangle> triangles;std::vector<uint32_t> indices;std::vector<Node> nodes;};
    std::array<TriangleMesh,3> meshes_;
    uint64_t mesh_hash_=0;
    uint32_t build_mesh_node(TriangleMesh& mesh,uint32_t first,uint32_t count);
    bool intersect_mesh(uint8_t kind,V3 origin,V3 direction,float&distance,V3&normal) const;
    struct ProbeRay {V3 direction{};Hit static_hit{};std::array<float,24> transport{};};
    std::vector<Shape> shapes_;
    std::vector<uint32_t> static_indices_,moving_indices_;
    std::vector<Node> nodes_;
    std::vector<IrradianceProbe> probes_;
    std::vector<ProbeRay> rays_;
    std::vector<BakedSurface> surfaces_;
    std::vector<BakedUv> lightmap_uv_;
    std::vector<std::array<uint64_t,24>> probe_transport_hashes_;
    std::array<uint64_t,24> light_transforms_{},light_values_{};
    std::vector<uint64_t> probe_epochs_;
    std::vector<float> probe_priorities_;
    uint64_t update_epoch_=0;
    ProbeGrid grid_{};BakeMetadata metadata_{};
    uint32_t cursor_=0,geometry_revision_=1;
    uint32_t build_node(uint32_t first,uint32_t count);
    Hit trace(V3 origin,V3 direction,float distance,bool static_geometry=true,bool moving_geometry=true) const;
    void intersect(const Shape& shape,V3 origin,V3 direction,Hit& hit) const;
    V3 direct(V3 position,V3 normal,std::span<const ShowcaseLight> lights,bool static_lights,bool moving_geometry=true) const;
    float transport(V3 position,V3 normal,const ShowcaseLight& light,bool moving_geometry=true) const;
    V3 bake_path(V3 position,V3 normal,std::span<const ShowcaseLight> lights,uint32_t seed) const;
    void bake(std::span<const ShowcaseObject> objects,std::span<const ShowcaseLight> lights);
};
}
