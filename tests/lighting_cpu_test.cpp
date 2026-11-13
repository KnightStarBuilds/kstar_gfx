#include "../src/demos/showcase_world.hpp"
#include "../tools/include/kgfx_asset_format.h"
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <filesystem>
#include <new>
#include <vector>
namespace {
std::atomic<size_t> allocations{0};
void require(bool value,const char*message){if(!value){std::fprintf(stderr,"%s\n",message);std::exit(1);}}
float energy(demo::V3 v){return v.x+v.y+v.z;}
bool same(demo::V3 a,demo::V3 b){return demo::dot(a-b,a-b)<1e-10f;}
float dynamic_energy(const demo::LightingCpu&cpu){float result=0;for(const auto&p:cpu.probes())result+=energy(p.dynamic_indirect);return result;}
}
void* operator new(size_t n){++allocations;if(void*p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void* operator new[](size_t n){return ::operator new(n);}
void operator delete(void*p)noexcept{std::free(p);}void operator delete[](void*p)noexcept{std::free(p);}
int main(int argc,char**argv){
    using namespace demo;
    ShowcaseLight physical;physical.type=LightType::Point;physical.units=LightIntensityUnit::Lumen;physical.intensity=4*pi;
    require(std::abs(light_intensity_candela(physical)-1)<1e-6f,"Point flux must cover four pi steradians");
    physical.type=LightType::Spot;physical.spot_cosine=.5f;physical.intensity=pi;require(std::abs(light_intensity_candela(physical)-1)<1e-6f,"Spot lumens must normalize over the authored outer cone");
    physical.type=LightType::Rect;physical.intensity=2*pi;physical.casts_shadow=true;require(light_shadow_faces(physical)==5&&std::abs(light_intensity_candela(physical)-1)<1e-6f,"Rect lights use a hemisphere and atomic five-face shadows");
    physical.units=LightIntensityUnit::Luminance;physical.width=2;physical.height=3;physical.intensity=4;require(light_intensity_candela(physical)==24,"Rect luminance must scale by emitting area");
    physical.type=LightType::Sky;require(light_shadow_faces(physical)==0,"Sky visibility comes from probes, not local atlas faces");
    physical.use_temperature=true;physical.temperature=2000;require(light_color(physical).x>light_color(physical).z,"Warm temperature must affect chromaticity");
    LightingCpu sky_visibility;ProbeGrid sky_grid{{0,0,0},{1,1,1},{1,1,1}};sky_visibility.initialize({}, {},sky_grid,false);
    for(float visible:sky_visibility.probes()[0].sky_visibility)require(visible>.95f,"Open probes must see the Sky hemisphere");
    ShowcaseObject enclosure;enclosure.id=900;enclosure.transform=scale({4,4,4});sky_visibility.initialize(std::span(&enclosure,1),{},sky_grid,false);
    for(float visible:sky_visibility.probes()[0].sky_visibility)require(visible==0,"Closed geometry must block direct Sky visibility");
    std::vector<ShowcaseObject> objects(3);objects[0].id=1;objects[0].transform=translate({0,-.15f,0})*scale({8,.3f,8});objects[0].material.base={.7f,.7f,.7f,1};
    objects[1].id=2;objects[1].transform=translate({0,2,-3.5f})*scale({8,4,.3f});objects[1].material.base={.7f,.4f,.2f,1};
    objects[2].id=3;objects[2].transform=translate({5,1,0})*scale({1,2,1});objects[2].movable=true;
    std::vector<ShowcaseLight> lights(2);lights[0].id=1;lights[0].position={-1,2,0};lights[0].radius=12;lights[0].intensity=30;lights[0].mobility=LightMobility::Static;
    lights[1]=lights[0];lights[1].id=2;lights[1].position={1,2,0};lights[1].mobility=LightMobility::Stationary;lights[1].color={1,.2f,.1f};
    ProbeGrid grid{{-1,.5f,-1},{1,1,1},{3,2,3}};LightingCpu cpu;cpu.initialize(objects,lights,grid,true);
    require(cpu.metadata().valid&&cpu.metadata().static_light_count==1&&cpu.metadata().diffuse_bounces==3,"Bake must include exactly Static lights and three diffuse bounces");
    require(cpu.surfaces().size()==2,"Moving geometry must not enter the baked receiver payload");
    require(cpu.occluded({0,1,-3},{0,1,-4}),"BVH traversal must find opaque wall intersections");require(!cpu.occluded({0,1,0},{0,3,0}),"Unobstructed visibility must remain open");
    for(int i=1;i<=12;++i)cpu.update(objects,lights,uint64_t(i));float initial=dynamic_energy(cpu);require(initial>0,"Stationary light must produce dynamic diffuse bounce");
    const uint64_t original_hash=LightingCpu::scene_hash(objects,lights);lights[1].intensity*=2;require(LightingCpu::scene_hash(objects,lights)==original_hash,"Stationary intensity edits must not stale a Static bake");
    objects[0].material.surface=ShowcaseSurface::Glass;require(LightingCpu::scene_hash(objects,lights)!=original_hash,"Opaque-to-glass edits must invalidate baked geometry");objects[0].material.surface=ShowcaseSurface::Opaque;
    objects[0].material.emissive={1,0,0};const auto emissive_hash=LightingCpu::scene_hash(objects,lights);require(emissive_hash!=original_hash,"Emissive geometry membership must invalidate the bake");objects[0].material.emissive={0,2,0};require(LightingCpu::scene_hash(objects,lights)==emissive_hash,"Fixture emission edits must retain unchanged bake geometry");objects[0].material.emissive={};
    size_t before=allocations.load();for(int i=13;i<=24;++i)cpu.update(objects,lights,uint64_t(i));require(allocations.load()==before,"Probe updates must not allocate");require(std::abs(dynamic_energy(cpu)/initial-2)<.01f,"Stationary intensity must update bounce using cached transport");
    lights[1].color={0,0,1};for(int i=25;i<=36;++i)cpu.update(objects,lights,uint64_t(i));for(const auto&p:cpu.probes())require(p.dynamic_indirect.x==0&&p.dynamic_indirect.y==0,"Stationary color changes must update indirect chromaticity");
    lights[1].position={-2,2,1};for(int i=37;i<=48;++i)cpu.update(objects,lights,uint64_t(i));LightingCpu fresh;fresh.initialize(objects,lights,grid,false);for(int i=37;i<=48;++i)fresh.update(objects,lights,uint64_t(i));require(std::abs(dynamic_energy(cpu)-dynamic_energy(fresh))<1e-4f,"Changed stationary transforms must invalidate every probe's transport cache");
    objects[2].transform=translate({0,1,0})*scale({1,2,1});++objects[2].geometry_revision;cpu.update(objects,lights,49);require(cpu.occluded({-2,1,0},{2,1,0}),"Moving blocker revisions must invalidate visibility");require(!cpu.occluded({-2,1,0},{2,1,0},false),"Static transport must exclude moving blocker geometry");
    auto directory=std::filesystem::path(argc>1?argv[1]:".tmp");std::filesystem::create_directories(directory);auto file=directory/"lighting-test.kglight";
    require(cpu.write_bake(file.string().c_str()),"Companion bake payload must serialize");require(cpu.read_bake(file.string().c_str(),objects,lights),"Companion bake must reload with matching static scene hash");
    lights[0].intensity+=1;require(!cpu.read_bake(file.string().c_str(),objects,lights),"Static intensity edits must reject stale companion data");lights[0].intensity-=1;objects[0].geometry_revision++;require(!cpu.read_bake(file.string().c_str(),objects,lights),"Static geometry revisions must reject stale companion data");std::filesystem::remove(file);
    // An open triangular prop intentionally occupies only half its bounds.
    // A box proxy would wrongly block the upper ray; imported triangles must not.
    kgfx_model_file mesh_header{};std::memcpy(mesh_header.magic,"KGMODEL",7);mesh_header.version=KGFX_ASSET_VERSION;mesh_header.vertex_count=3;mesh_header.index_count=3;mesh_header.vertices_offset=sizeof(mesh_header);mesh_header.indices_offset=sizeof(mesh_header)+3*sizeof(kgfx_asset_vertex);for(unsigned n=0;n<3;++n){mesh_header.bounds_min[n]=-.5f;mesh_header.bounds_max[n]=.5f;}
    std::array<kgfx_asset_vertex,3> vertices{};const V3 positions_mesh[]={{.5f,-.5f,-.5f},{.5f,.5f,-.5f},{.5f,-.5f,.5f}};for(unsigned n=0;n<3;++n){vertices[n].position[0]=positions_mesh[n].x;vertices[n].position[1]=positions_mesh[n].y;vertices[n].position[2]=positions_mesh[n].z;}const uint32_t indices[]={0,1,2};
    for(const char*name:{"modular_industrial_pipes_01_pipe01.kgmodel","power_box_01.kgmodel","Barrel_01.kgmodel"}){FILE*out=std::fopen((directory/name).string().c_str(),"wb");require(out,"Cannot create triangle test fixture");std::fwrite(&mesh_header,1,sizeof(mesh_header),out);std::fwrite(vertices.data(),sizeof(kgfx_asset_vertex),3,out);std::fwrite(indices,4,3,out);std::fclose(out);}
    ShowcaseObject imported_object;imported_object.id=1;imported_object.geometry=ShowcaseGeometry::PowerBox;LightingCpu imported;require(imported.load_geometry(directory.string().c_str()),"Imported CPU triangle geometry must load");imported.initialize(std::span(&imported_object,1),lights,grid,true);require(!imported.occluded({1,.3f,.3f},{-1,.3f,.3f}),"Imported geometry must preserve empty space inside its bounds");require(imported.occluded({1,-.3f,-.3f},{-1,-.3f,-.3f}),"Imported triangle BVH must detect actual mesh surfaces");
    require(imported.write_bake(file.string().c_str()),"Imported geometry bake must serialize");FILE*changed_mesh=std::fopen((directory/"power_box_01.kgmodel").string().c_str(),"r+b");require(changed_mesh,"Cannot change test mesh");std::fseek(changed_mesh,long(sizeof(mesh_header)),SEEK_SET);float changed_position=.4f;std::fwrite(&changed_position,4,1,changed_mesh);std::fclose(changed_mesh);require(imported.load_geometry(directory.string().c_str()),"Modified triangle geometry must load");require(!imported.read_bake(file.string().c_str(),std::span(&imported_object,1),lights),"Imported mesh byte changes must invalidate the bake hash");std::filesystem::remove(file);for(const char*name:{"modular_industrial_pipes_01_pipe01.kgmodel","power_box_01.kgmodel","Barrel_01.kgmodel"})std::filesystem::remove(directory/name);
    ShowcaseWorld baked("lighting_baked",17,64),dynamic("lighting_dynamic",17,64);auto a=baked.frame(),b=dynamic.frame();
    require(a.objects.size()==b.objects.size()&&a.lights.size()==b.lights.size()&&same(a.eye,b.eye)&&same(a.target,b.target),"Lighting pair must have matching geometry and camera");
    require(a.bake.valid&&!b.bake.valid&&b.baked_surfaces.empty(),"Dynamic scene must consume no baked scene lighting");require(a.irradiance_probes.size()==160&&b.irradiance_probes.size()==160,"Probe grid must leave spare jobs for changed regions within the 192-probe budget");
    uint32_t faces=0,statics=0;for(size_t i=0;i<a.lights.size();++i){const auto&l=a.lights[i];if(l.type!=LightType::Directional)faces+=light_shadow_faces(l);if(l.mobility==LightMobility::Static){++statics;require(b.lights[i].mobility==LightMobility::Stationary,"Equivalent Static fixtures become Stationary in dynamic scene");}}
    require(statics==2&&faces<=32,"Scene fixtures must fit complete atomic local shadow allocations");
    for(size_t i=0;i<a.objects.size();++i){require(a.objects[i].id==i+1&&b.objects[i].id==i+1,"Object IDs must be stable across matched scenes");for(int k=0;k<16;++k)require(a.objects[i].transform.v[k]==b.objects[i].transform.v[k],"Matched objects must have identical transforms");}
    before=allocations.load();for(int n=0;n<120;++n){baked.tick();dynamic.tick();baked.update_lighting();dynamic.update_lighting();baked.frame();dynamic.frame();if(n>=12)for(const auto&p:baked.frame().irradiance_probes)require(baked.state().tick-p.last_update<12,"Changed-region scheduling must preserve every probe deadline");}require(allocations.load()==before,"Lighting pair simulation and presentation must allocate no ordinary-frame storage");
    for(const auto&p:baked.frame().irradiance_probes)require(baked.state().tick-p.last_update<12,"Every irradiance probe must refresh within twelve updates");
    dynamic.reset();for(uint64_t presented=1;presented<=30;++presented){dynamic.tick();dynamic.tick();for(const auto&p:dynamic.frame().irradiance_probes)require(p.last_update<presented,"Simulation ticks must not spend the presentation probe budget");dynamic.update_lighting();unsigned changed=0;for(const auto&p:dynamic.frame().irradiance_probes){changed+=p.last_update==presented;if(presented>=12)require(presented-p.last_update<12,"Probe deadline must use rendered frames at 30 FPS");}require(changed==16,"Each rendered frame must update exactly sixteen probes");}
    WorldInput freeze;freeze.toggle_animation=true;baked.tick(freeze);auto frozen=baked.frame();std::vector<V3> positions;for(const auto&l:frozen.lights)positions.push_back(l.position);auto animation_tick=baked.state().animation_tick;for(int n=0;n<8;++n)baked.tick();require(baked.state().animation_tick==animation_tick,"Animation freeze must preserve time");for(size_t i=0;i<positions.size();++i)require(same(positions[i],baked.frame().lights[i].position),"Animation freeze must preserve fixture transforms");
    WorldInput edit;edit.cycle_light_color=true;edit.light_intensity_delta=.5f;auto fixed_color=baked.frame().lights[0].color;float fixed_intensity=baked.frame().lights[0].intensity;baked.tick(edit);require(same(fixed_color,baked.frame().lights[0].color)&&fixed_intensity==baked.frame().lights[0].intensity,"Stationary controls must never alter Static fixtures");
    dynamic.match_view(baked);require(same(dynamic.frame().eye,baked.frame().eye)&&dynamic.state().animation_tick==baked.state().animation_tick,"Matched view switch must preserve camera and animation time");
    baked.reset();require(baked.state().animation_tick==0&&!baked.state().animation_frozen&&baked.state().stationary_intensity==1,"Reset must restore authored lighting controls");
    std::puts("Lighting CPU: physical units, static-only bake, stale payloads, cached dynamic bounce, blockers, scene parity, probe deadlines and allocations passed");
}
