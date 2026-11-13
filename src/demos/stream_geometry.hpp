#pragma once
#include "math.hpp"
#include "water_surface.hpp"
#include <array>
#include <cstdint>

namespace demo {
inline constexpr uint32_t max_water_streams=8;
struct WaterStream {
    V3 origin{},velocity{0,-1,0};
    float radius=.045f,max_time=1.5f,surface_height=.035f;
};
// Coherent ballistic water core. Fine particles supply breakup and the real
// collision impulses. Mesh ownership stays with the renderer, with no heap
// work during tessellation. All positions/normals are world space.
struct WaterStreamGeometry {
    static constexpr uint32_t segments=24,sides=8,vertex_count=(segments+1)*(sides+1),index_count=segments*sides*6;
    struct Vertex {V3 position,normal;V2 uv;};
    std::array<Vertex,vertex_count> vertices{};
    std::array<uint32_t,index_count> indices{};
    float flight_time=0;
    WaterStreamGeometry(){
        uint32_t cursor=0;
        for(uint32_t ring=0;ring<segments;++ring)for(uint32_t side=0;side<sides;++side){
            const uint32_t a=ring*(sides+1)+side,b=a+sides+1;
            for(uint32_t i:{a,b,a+1,a+1,b,b+1})indices[cursor++]=i;
        }
    }
    static V3 trajectory(const WaterStream& stream,float time){return stream.origin+stream.velocity*time+V3{0,-4.9f*time*time,0};}
    void update(const WaterStream& stream){
        constexpr float gravity=9.8f;
        const float discriminant=stream.velocity.y*stream.velocity.y+2*gravity*std::max(0.f,stream.origin.y-stream.surface_height);
        flight_time=std::clamp((stream.velocity.y+std::sqrt(discriminant))/gravity,.001f,std::max(.001f,stream.max_time));
        const float initial_speed=std::max(.1f,std::sqrt(dot(stream.velocity,stream.velocity)));
        for(uint32_t ring=0;ring<=segments;++ring){
            const float fraction=float(ring)/segments,time=fraction*flight_time;
            const V3 center=trajectory(stream,time),velocity=stream.velocity+V3{0,-gravity*time,0},direction=unit(velocity);
            V3 right=unit(cross(direction,std::abs(direction.y)>.9f?V3{1,0,0}:V3{0,1,0})),up=unit(cross(right,direction));
            const float speed=std::max(.1f,std::sqrt(dot(velocity,velocity)));
            const float radius=std::max(.002f,stream.radius)*std::sqrt(initial_speed/speed);
            for(uint32_t side=0;side<=sides;++side){
                const float angle=float(side)/sides*2*pi;const V3 normal=right*std::cos(angle)+up*std::sin(angle);
                vertices[ring*(sides+1)+side]={center+normal*radius,normal,{float(side)/sides,fraction}};
            }
        }
    }
};
struct SplashCrownGeometry {
    static constexpr uint32_t sides=24,vertex_count=(sides+1)*3,index_count=sides*12;
    using Vertex=WaterStreamGeometry::Vertex;
    std::array<Vertex,vertex_count> vertices{};
    std::array<uint32_t,index_count> indices{};
    SplashCrownGeometry(){uint32_t cursor=0;for(uint32_t ring=0;ring<2;++ring)for(uint32_t side=0;side<sides;++side){uint32_t a=ring*(sides+1)+side,b=a+sides+1;for(uint32_t i:{a,b,a+1,a+1,b,b+1})indices[cursor++]=i;}}
    void update(const WaterSurface::Crown& crown,float alpha=1){
        const float age=std::max(0.f,crown.age+(std::clamp(alpha,0.f,1.f)-1)*WaterSurface::fixed_step),phase=std::clamp(age/crown.lifetime,0.f,1.f);
        const float radius=crown.radius*(.6f+phase*1.7f),height=crown.strength*4*phase*(1-phase);
        for(uint32_t ring=0;ring<3;++ring)for(uint32_t side=0;side<=sides;++side){
            const float angle=float(side)/sides*2*pi,lobe=.72f+.28f*std::cos(angle*6),r=radius*(ring==0?.65f:ring==1?1.f:1.18f),y=ring==1?height*lobe:.003f;
            const V3 radial{std::cos(angle),0,std::sin(angle)},normal=unit(radial*(ring==0?-1.f:1.f)+V3{0,.35f,0});
            vertices[ring*(sides+1)+side]={crown.origin+radial*r+V3{0,y,0},normal,{float(side)/sides,float(ring)*.5f}};
        }
    }
};
}
