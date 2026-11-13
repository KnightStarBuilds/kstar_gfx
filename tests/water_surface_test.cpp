#include "../src/demos/water_surface.hpp"
#include <bit>
#include <cstdio>
#include <cstdlib>
#include <limits>

namespace {
void require(bool ok,const char* message){if(!ok){std::fprintf(stderr,"%s\n",message);std::exit(1);}}
uint64_t hash(const demo::WaterSurface& water){uint64_t h=1469598103934665603ull;for(uint32_t z=0;z<water.side;++z)for(uint32_t x=0;x<water.side;++x){auto s=water.vertex(x,z);for(float f:{s.height,s.normal.x,s.normal.y,s.normal.z,s.foam}){h^=std::bit_cast<uint32_t>(f);h*=1099511628211ull;}}return h;}
}
int main(){
    using namespace demo;
    WaterSurface water;water.reset({2,.035f,-3},{6.4f,8});
    require(water.vertex_count==4225&&water.index_count==24576,"Pool must be 64x64 cells with 65x65 vertices");
    const auto initial=hash(water);
    for(int i=0;i<300;++i)water.step();
    require(hash(water)==initial,"Unforced water must stay still rather than generating cosmetic sine waves");
    require(!water.impact({200,0,0},1)&&hash(water)==initial,"Out-of-pool impacts must not disturb the shoreline");
    require(!water.impact(water.center(),std::numeric_limits<float>::infinity()),"Nonfinite impulses must be rejected");
    require(water.impact(water.center(),2,.4f),"An actual impact must enter the simulation");
    require(water.crowns()[0].strength>0,"A substantial impact must create a bounded splash crown");
    const auto before=water.sample(water.center());water.step();
    const auto previous=water.sample(water.center(),0),current=water.sample(water.center(),1),middle=water.sample(water.center(),.5f);
    require(previous.height==before.height&&current.height>previous.height,"Impact velocity must deform geometry on the next fixed tick");
    require(std::abs(middle.height-(previous.height+current.height)*.5f)<1e-6f,"Surface interpolation must match geometry and depth");
    require(current.foam>.1f,"Impacts must deposit persistent foam");
    for(int i=0;i<120;++i)water.step();
    require(water.crowns()[0].strength==0,"Splash crowns must retire after their ballistic lifetime");
    require(std::abs(water.sample(water.center()+V3{.8f,0,0}).height-.035f)>.00001f,"Ripples must propagate beyond the impact footprint");
    float total=0;for(uint32_t z=0;z<water.side;++z)for(uint32_t x=0;x<water.side;++x){auto s=water.vertex(x,z);total+=s.foam;require(std::abs(dot(s.normal,s.normal)-1)<1e-5f&&std::isfinite(s.height),"Evolved surface normals and heights must remain finite");}
    require(total>.1f,"Foam must outlive a transient impact");
    WaterSurface repeat;repeat.reset({2,.035f,-3},{6.4f,8});repeat.impact(repeat.center(),2,.4f);for(int i=0;i<121;++i)repeat.step();require(hash(repeat)==hash(water),"Equal impact/tick histories must replay deterministically");
    water.reset({2,.035f,-3},{6.4f,8});require(hash(water)==initial&&water.tick()==0,"Reset must clear waves and foam");
    water.reset({0,0,0},{3,3});for(int i=0;i<6000;++i){if(i%4==0)water.impact({0,0,0},3,.1f);water.step();}
    double level=0;for(uint32_t z=0;z<water.side;++z)for(uint32_t x=0;x<water.side;++x){auto s=water.vertex(x,z);level+=s.height;require(std::isfinite(s.height)&&std::abs(s.height)<=.221f&&s.foam>=0&&s.foam<=1,"Repeated impulses must stay bounded at the minimum CFL extent");}
    require(std::abs(level/water.vertex_count)<.0001,"A long impact sequence must preserve the sealed basin mean level");
    std::puts("water surface: propagation, interpolation, foam, bounds and deterministic replay passed");
}
