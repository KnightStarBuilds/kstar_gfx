#include "../src/demos/stream_geometry.hpp"
#include <cmath>
#include <cstdio>
#include <cstdlib>

void require(bool ok,const char*message){if(!ok){std::fprintf(stderr,"%s\n",message);std::exit(1);}}
int main(){
    using namespace demo;
    WaterStream stream;stream.origin={1,3,2};stream.velocity={1.2f,0,-.2f};
    WaterStreamGeometry geometry;geometry.update(stream);
    require(std::abs(WaterStreamGeometry::trajectory(stream,geometry.flight_time).y-stream.surface_height)<1e-5f,"The coherent core must terminate at the impact surface");
    for(auto index:geometry.indices)require(index<geometry.vertex_count,"Stream indices must address valid vertices");
    for(uint32_t ring=0;ring<=geometry.segments;++ring){
        const auto a=geometry.vertices[ring*(geometry.sides+1)],b=geometry.vertices[ring*(geometry.sides+1)+geometry.sides];
        require(dot(a.position-b.position,a.position-b.position)<1e-10f,"Ring seam must close without a geometry gap");
        const auto center=WaterStreamGeometry::trajectory(stream,geometry.flight_time*float(ring)/geometry.segments);
        float radius2=dot(a.position-center,a.position-center);
        require(radius2>0&&radius2<.02f&&std::abs(dot(a.normal,a.normal)-1)<1e-5f,"Falling jets must retain bounded round cross sections and finite normals");
    }
    stream.velocity={0,-2,0};geometry.update(stream);for(const auto& v:geometry.vertices)require(std::isfinite(v.position.x)&&std::isfinite(v.normal.x),"Vertical jets must not produce a degenerate tangent basis");
    SplashCrownGeometry crown;WaterSurface::Crown state{{1,.035f,2},.18f,.1f,.2f,.55f};crown.update(state,.5f);
    for(auto index:crown.indices)require(index<crown.vertex_count,"Crown indices must address valid vertices");
    require(crown.vertices[crown.sides+1].position.y>.08f,"An impact crown must rise above the pool");
    std::puts("stream geometry: ballistic impact, watertight rings and vertical tangent basis passed");
}
