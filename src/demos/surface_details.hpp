#pragma once
#include "math.hpp"
#include <cstdint>

namespace demo {
// Each cell is independently UV authored in effects/surface_masks.kgtex.
// R is wet coating, G roughness variation, B normal relief, A coverage.
enum class SurfaceMask : uint8_t { None, Porous, Leak, Footprints, Grime };
struct MaterialSurfaceMask {
    SurfaceMask texture=SurfaceMask::None;
    V4 uv{1,1,0,0}; // Independent scale.xy and offset.zw, before atlas mapping.
    float wetness=0,roughness=0;
};
struct ShowcaseDecal {
    uint32_t id=0,receiver_mask=1,layer=0;
    // Projector coordinates occupy [-.5,.5] on all axes. Surface UV uses XZ;
    // the shallow Y axis limits projection to the intended receiver surface.
    M4 world_to_decal=identity();
    V4 color{1,1,1,1};
    SurfaceMask texture=SurfaceMask::Grime;
    float normal=0,roughness=0,wetness=0;
};
inline constexpr uint32_t max_showcase_decals=128,max_draw_decals=8;
}
