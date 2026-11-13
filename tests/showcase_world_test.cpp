#include "../src/demos/showcase_world.hpp"
#include <atomic>
#include <bit>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <new>
#include <stdexcept>

namespace {
std::atomic<size_t> allocations{0};
void require(bool condition,const char*message){if(!condition){std::fprintf(stderr,"%s\n",message);std::exit(1);}}
bool close(float a,float b){return std::abs(a-b)<.00001f;}
uint64_t hash(const demo::ShowcaseFrame& frame){
    uint64_t h=1469598103934665603ull;
    auto add=[&](float value){h^=std::bit_cast<uint32_t>(value);h*=1099511628211ull;};
    for(const auto&p:frame.particles){add(p.position.x);add(p.position.y);add(p.position.z);add(p.velocity.x);add(p.velocity.y);add(p.velocity.z);add(p.age);add(p.size);add(float(p.kind));}
    add(frame.eye.x);add(frame.eye.y);add(frame.eye.z);add(frame.time);return h;
}
void walk_to(demo::ShowcaseWorld& world,demo::V3 destination){
    using namespace demo;
    for(int n=0;n<900;++n){
        auto frame=world.frame();V3 delta=destination-frame.eye;delta.y=0;
        if(dot(delta,delta)<.015f)return;
        const V3 facing=frame.target-frame.eye;WorldInput input;input.move_forward=1;
        input.look_x=std::remainder(std::atan2(delta.x,-delta.z)-std::atan2(facing.x,-facing.z),2*pi);world.tick(input);
    }
    auto eye=world.frame().eye;std::fprintf(stderr,"Walking route blocked at %.2f,%.2f toward %.2f,%.2f\n",eye.x,eye.z,destination.x,destination.z);std::exit(1);
}
}
void* operator new(std::size_t size){++allocations;if(void*p=std::malloc(size?size:1))return p;throw std::bad_alloc();}
void* operator new[](std::size_t size){return ::operator new(size);}
void operator delete(void*p) noexcept{std::free(p);}
void operator delete[](void*p) noexcept{std::free(p);}

int main(){
    using namespace demo;
    require(ShowcaseWorld::supports_scene("facility")&&!ShowcaseWorld::supports_scene("pbr"),"Showcase recognition must preserve legacy scene dispatch");
    bool rejected=false;try{ShowcaseWorld invalid("missing");}catch(const std::invalid_argument&){rejected=true;}require(rejected,"Unknown showcase scenes must fail before resources are replaced");
    for(auto scene:{"facility","glass_lab","waterworks","boiler_room"}){
        ShowcaseWorld world(scene,73);auto first=world.frame();
        require(first.objects.size()>40&&first.objects.size()<512,"Each map must contain authored modules within the draw capacity");
        require(!first.lights.empty(),"Every showcase needs local industrial lighting");
        require(!world.state().inspection,"Default scene view must start in walking mode");
        world.set_view("glass");require(world.state().inspection,"Fixed photographic views must use inspection mode");
        world.set_view("overview");require(!world.state().inspection,"Overview must restore walking controls");
        const size_t allocated=allocations.load();
        for(int n=0;n<600;++n){world.tick();world.frame(.4f);}
        require(allocations.load()==allocated,"World simulation/interpolation must not allocate after scene construction");
        require(world.state().particle_count>0&&world.state().particle_count<=ShowcaseWorld::max_particles,"Every map must maintain bounded live effects");
        const auto end=hash(world.frame());
        world.reset();for(int n=0;n<600;++n)world.tick();require(hash(world.frame())==end,"Reset must restore authored state, simulation tick, and particle RNG");
        ShowcaseWorld duplicate(scene,73);for(int n=0;n<600;++n)duplicate.tick();require(hash(duplicate.frame())==end,"Equal seeds/ticks must reproduce camera and particles");
        world.set_particle_limit(32);require(world.state().particle_count<=32,"Reducing a quality budget must immediately trim the live pool");
        for(int n=0;n<120;++n)world.tick();require(world.frame().particles.size()<=32,"Emitters must respect reduced particle budgets");
        world.set_particle_limit(0);world.tick();require(world.frame().particles.empty(),"A zero-capacity pool must safely suppress emissions");
    }
    ShowcaseWorld realtime("waterworks",19),fixed("waterworks",19);
    for(int n=0;n<30;++n)realtime.advance(1.0/30.0);for(int n=0;n<60;++n)fixed.tick();
    require(realtime.state().tick==60&&hash(realtime.frame())==hash(fixed.frame()),"30 FPS presentation must advance exactly 60 deterministic ticks per second");
    const auto paused=hash(realtime.frame());realtime.advance(10,{},true);require(hash(realtime.frame())==paused,"A paused menu or unfocused window must not advance effects");
    realtime.advance(std::numeric_limits<double>::infinity());require(hash(realtime.frame())==paused,"Nonfinite elapsed time must not corrupt the accumulator");
    realtime.advance(10);require(realtime.state().tick==68,"Long stalls must be bounded to eight catch-up ticks");
    require(realtime.interpolation()>=0&&realtime.interpolation()<1,"Dropped stall time must leave a bounded interpolation factor");
    for(auto scene:{"facility","glass_lab","waterworks","boiler_room"}){
        ShowcaseWorld world(scene,3);world.set_sequence("interact");for(int n=0;n<119;++n)world.tick();
        require(world.state().lights_on&&world.state().water_on&&!world.state().sprinklers_on&&world.state().glass_variant==0,"Scripted before-state must remain stable through tick 119");
        world.tick();const auto state=world.state();
        if(world.scene()=="facility")require(!state.lights_on&&!state.water_on&&state.sprinklers_on&&state.glass_variant==1,"Facility sequence must demonstrate every station at tick 120");
        if(world.scene()=="glass_lab")require(state.glass_variant==1,"Glass sequence must cycle the pane materials");
        if(world.scene()=="waterworks")require(!state.water_on,"Water sequence must close the valve");
        if(world.scene()=="boiler_room"){
            require(state.sprinklers_on,"Boiler sequence must activate the sprinklers");for(int n=0;n<240;++n)world.tick();
            require(world.state().fire_level<.13f,"Sprinklers must visibly diminish fire over simulated time");
            bool smoke_volume=false,spray=false;for(const auto&v:world.frame().volumes)smoke_volume|=v.asset==ShowcaseVolumeAsset::ChimneySmoke;for(const auto&p:world.frame().particles)spray|=p.kind==ShowcaseParticleKind::Spray;require(smoke_volume&&spray,"Suppression must retain volumetric vapor and produce sprinkler droplets");
        }
    }
    // Walk into a real observation pane. A point camera would cross it after
    // around 130 ticks; the capsule must stop in front while sliding laterally.
    ShowcaseWorld glass("glass_lab");WorldInput forward;forward.move_forward=1;
    for(int n=0;n<170;++n)glass.tick(forward);
    require(glass.frame().eye.z>-.0f-1.4f+.27f,"Walking collision must keep the capsule in front of glass");
    auto travel_distance=[](bool sprint,bool diagonal){
        ShowcaseWorld world("facility",1,0);auto start=world.frame();V3 direction=start.target-start.eye;const V3 origin=start.eye;
        WorldInput movement;movement.move_forward=1;movement.move_right=diagonal?1.f:0.f;movement.sprint=sprint;
        movement.look_x=-std::atan2(direction.x,-direction.z);
        for(int n=0;n<20;++n){world.tick(movement);movement.look_x=0;}
        const V3 delta=world.frame().eye-origin;return std::sqrt(delta.x*delta.x+delta.z*delta.z);
    };
    const float walking=travel_distance(false,false),running=travel_distance(true,false),diagonal_running=travel_distance(true,true);
    require(std::abs(running-walking*1.8f)<.0001f,"Sprint must increase straight-line speed after direction normalization");
    require(std::abs(diagonal_running-running)<.0001f,"Diagonal sprint must preserve the same maximum travel speed");
    ShowcaseWorld sprint_collision("glass_lab",1,0);auto initial_glass=sprint_collision.frame();
    WorldInput sprint_forward;sprint_forward.move_forward=1;sprint_forward.sprint=true;
    sprint_forward.look_x=-std::atan2(initial_glass.target.x-initial_glass.eye.x,initial_glass.eye.z-initial_glass.target.z);
    for(int n=0;n<200;++n){sprint_collision.tick(sprint_forward);sprint_forward.look_x=0;}
    require(sprint_collision.frame().eye.z>-1.13f,"Sprinting must not tunnel through the thin observation pane");
    ShowcaseWorld inspection("glass_lab");WorldInput toggle;toggle.toggle_inspection=true;inspection.tick(toggle);
    require(inspection.state().inspection,"Camera toggle must enter inspection mode");inspection.tick(toggle);require(!inspection.state().inspection&&close(inspection.frame().eye.y,1.7f),"Returning to walking must restore eye height");
    ShowcaseWorld water("waterworks");WorldInput ramp;ramp.move_forward=1;auto initial=water.frame();V3 direction=initial.target-initial.eye;ramp.look_x=-std::atan2(direction.x,-direction.z);
    bool recessed_bottom=false;
    for(const auto&object:initial.objects){
        if(object.geometry!=ShowcaseGeometry::Cube)continue;
        const auto&m=object.transform;
        const bool covers_pool_center=std::abs(m.v[12]+2.25f)<std::abs(m.v[0])*.5f&&std::abs(m.v[14])<std::abs(m.v[10])*.5f;
        if(covers_pool_center&&m.v[13]<-1)recessed_bottom=true;
        require(!covers_pool_center||m.v[13]+std::abs(m.v[5])*.5f<-.5f||m.v[13]>1,"Pool floor must not sit just below the water and turn the whole surface into shoreline foam");
    }
    require(recessed_bottom,"Water must have an authored recessed basin for absorption and depth intersections");
    water.tick(ramp);ramp.look_x=0;for(int n=0;n<38;++n)water.tick(ramp);
    require(water.frame().eye.y>2.2f,"The pump-hall ramp must lift the walking capsule to catwalk height");
    ShowcaseWorld near_station("glass_lab");near_station.set_view("glass");WorldInput interact;interact.interact=true;near_station.tick(interact);
    require(near_station.state().glass_variant==1,"Inspection interaction must activate the nearest station");
    ShowcaseWorld seeded("boiler_room",99),other_seed("boiler_room",100);for(int n=0;n<60;++n){seeded.tick();other_seed.tick();}
    require(hash(seeded.frame())!=hash(other_seed.frame()),"Different seeds must change particle placement");
    // Emission arithmetic uses separate operations on ARM and x86. Water
    // ripples now arise from droplet impacts, so no periodic cards are emitted.
    ShowcaseWorld cross_architecture("facility",8675309,4096);
    for(int n=0;n<132;++n)cross_architecture.tick();
    size_t ripples=0;for(const auto&p:cross_architecture.frame().particles)if(p.kind==ShowcaseParticleKind::Ripple)++ripples;
    require(cross_architecture.state().particle_count==709&&ripples==0,"Fixed seed/tick emission counts must match ARM and x86 without FMA contraction");
    // Exercise the actual capsule route and reach each station using ordinary
    // walking input. This catches seams between independently authored rooms.
    ShowcaseWorld facility("facility",1,128);
    walk_to(facility,{-2.7f,0,15.7f});facility.tick(interact);require(!facility.state().lights_on,"Loading-yard lighting station must be reachable");
    for(V3 point:{V3{0,0,6},V3{0,0,1.5f},V3{3.8f,0,1.5f}})walk_to(facility,point);
    facility.tick(interact);require(facility.state().glass_variant==1,"Glass control station must be reachable");
    for(V3 point:{V3{5.8f,0,0},V3{5.8f,0,-4.5f},V3{0,0,-5.6f},V3{0,0,-7.7f},V3{3.6f,0,-7.7f},V3{3.6f,0,-15}})walk_to(facility,point);
    facility.tick(interact);require(!facility.state().water_on,"Pump station must be reachable across the authored catwalk ramp");
    for(V3 point:{V3{3.6f,0,-18.6f},V3{0,0,-18.6f},V3{0,0,-20},V3{0,0,-21.9f}})walk_to(facility,point);
    facility.tick(interact);require(facility.state().sprinklers_on,"Boiler sprinkler station must be reachable from the pump hall");
    walk_to(facility,{0,0,-27});
    std::puts("Showcase world: deterministic simulation, interactions, collision, budgets, and allocation checks passed");
}
