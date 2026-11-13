#include "lighting_cpu.hpp"
#include "showcase_world.hpp"
#include "../../tools/include/kgfx_asset_format.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <numeric>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <limits>

namespace demo {
namespace {
// Pinned console libc++ lacks bit_cast despite the C++23 language mode.
uint32_t float_bits(float value){uint32_t bits;std::memcpy(&bits,&value,sizeof(bits));return bits;}
float bits_float(uint32_t bits){float value;std::memcpy(&value,&bits,sizeof(value));return value;}
constexpr V3 axes[6]={{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}};
V3 multiply(V3 a,V3 b){return {a.x*b.x,a.y*b.y,a.z*b.z};}
V3 point(const M4&m,V3 p){return {m.v[0]*p.x+m.v[4]*p.y+m.v[8]*p.z+m.v[12],m.v[1]*p.x+m.v[5]*p.y+m.v[9]*p.z+m.v[13],m.v[2]*p.x+m.v[6]*p.y+m.v[10]*p.z+m.v[14]};}
V3 vector(const M4&m,V3 p){return {m.v[0]*p.x+m.v[4]*p.y+m.v[8]*p.z,m.v[1]*p.x+m.v[5]*p.y+m.v[9]*p.z,m.v[2]*p.x+m.v[6]*p.y+m.v[10]*p.z};}
V3 minimum(V3 a,V3 b){return {std::min(a.x,b.x),std::min(a.y,b.y),std::min(a.z,b.z)};}
V3 maximum(V3 a,V3 b){return {std::max(a.x,b.x),std::max(a.y,b.y),std::max(a.z,b.z)};}
float component(V3 p,unsigned n){return n==0?p.x:n==1?p.y:p.z;}
float random_float(uint32_t& state){state^=state<<13;state^=state>>17;state^=state<<5;return float(state>>8)*(1.f/16777216.f);}
V3 hemisphere(V3 n,uint32_t& state){float a=2*pi*random_float(state),r=std::sqrt(random_float(state));V3 tangent=unit(cross(std::abs(n.y)<.95f?V3{0,1,0}:V3{1,0,0},n)),bitangent=cross(n,tangent);return tangent*(r*std::cos(a))+bitangent*(r*std::sin(a))+n*std::sqrt(1-r*r);}
bool bounds(V3 lo,V3 hi,V3 p,V3 d,float maximum_distance){float near=0,far=maximum_distance;for(unsigned a=0;a<3;++a){float v=component(d,a),o=component(p,a),l=component(lo,a),h=component(hi,a);if(std::abs(v)<1e-9f){if(o<l||o>h)return false;}else{float t0=(l-o)/v,t1=(h-o)/v;if(t0>t1)std::swap(t0,t1);near=std::max(near,t0);far=std::min(far,t1);if(near>far)return false;}}return far>.0005f;}
uint64_t mix_hash(uint64_t h,uint64_t v){for(unsigned n=0;n<8;++n){h^=uint8_t(v>>(n*8));h*=1099511628211ull;}return h;}
uint64_t hash_float(uint64_t h,float f){return mix_hash(h,float_bits(f));}
uint64_t light_transform_hash(const ShowcaseLight&l){uint64_t h=l.id;for(float f:{l.position.x,l.position.y,l.position.z,l.direction.x,l.direction.y,l.direction.z,l.right.x,l.right.y,l.right.z,l.radius,l.width,l.height,l.spot_cosine,l.inner_cone_cosine,l.source_radius})h=hash_float(h,f);return mix_hash(mix_hash(h,uint64_t(l.type)),l.casts_shadow);}
}
// Two persistent jobs own disjoint probe ranges. The submission waits for the
// bounded batch before exposing a frame; scheduling never changes sample order.
struct LightingCpu::Workers {
    LightingCpu*owner;
    std::array<std::thread,2> threads;
    std::mutex mutex;std::condition_variable ready,finished;
    std::span<const ShowcaseLight> lights;
    uint64_t frame=0,generation=0;std::array<uint32_t,probes_per_update> indices{};uint32_t count=0,completed=0;bool stop=false;
    explicit Workers(LightingCpu*p):owner(p){for(uint32_t n=0;n<2;++n)threads[n]=std::thread([this,n]{uint64_t observed=0;for(;;){std::unique_lock lock(mutex);ready.wait(lock,[&]{return stop||generation!=observed;});if(stop)return;observed=generation;auto sources=lights;uint64_t tick=frame;auto selected=indices;uint32_t jobs=count;lock.unlock();for(uint32_t job=n;job<jobs;job+=2)owner->update_probe(selected[job],sources,tick);lock.lock();if(++completed==2)finished.notify_one();}});}
    ~Workers(){{std::lock_guard lock(mutex);stop=true;}ready.notify_all();for(auto&thread:threads)if(thread.joinable())thread.join();}
    void run(std::span<const uint32_t> selected,std::span<const ShowcaseLight>sources,uint64_t tick){std::unique_lock lock(mutex);std::copy(selected.begin(),selected.end(),indices.begin());count=uint32_t(selected.size());lights=sources;frame=tick;completed=0;++generation;ready.notify_all();finished.wait(lock,[&]{return completed==2;});}
};
LightingCpu::LightingCpu()=default;
LightingCpu::~LightingCpu(){workers_.reset();}
float light_intensity_candela(const ShowcaseLight&l){
    if(l.units==LightIntensityUnit::Luminance&&l.type==LightType::Rect)return l.intensity*std::max(0.f,l.width*l.height);
    // Lumens preserve flux when source area changes. Rect output covers a 2pi
    // hemisphere; luminance instead scales with the actual emitting area.
    if(l.units!=LightIntensityUnit::Lumen)return l.intensity;
    float solid_angle=l.type==LightType::Point?4*pi:l.type==LightType::Spot?2*pi*(1-std::clamp(l.spot_cosine,-.999f,.999f)):2*pi;
    return l.intensity/std::max(solid_angle,.001f);
}
V3 light_color(const ShowcaseLight&l){
    if(!l.use_temperature)return l.color;
    // Black-body approximation in display RGB, normalized at D65. Temperature
    // affects chromaticity only; authored intensity remains in photometric units.
    float t=std::clamp(l.temperature,1000.f,40000.f)/100;
    V3 c{t<=66?1.f:std::clamp(1.292936f*std::pow(t-60,-.133205f),0.f,1.f),t<=66?std::clamp(.390082f*std::log(t)-.631841f,0.f,1.f):std::clamp(1.129891f*std::pow(t-60,-.075515f),0.f,1.f),t>=66?1.f:t<=19?0.f:std::clamp(.543207f*std::log(t-10)-1.196254f,0.f,1.f)};
    c.y*=std::exp(-std::clamp(l.tint,-1.f,1.f));return multiply(l.color,c);
}
uint32_t light_shadow_faces(const ShowcaseLight&l){if(!l.casts_shadow)return 0;switch(l.type){case LightType::Point:return 6;case LightType::Spot:return 1;case LightType::Rect:return 5;case LightType::Directional:return 2;case LightType::Sky:return 0;}return 0;}
uint64_t LightingCpu::scene_hash(std::span<const ShowcaseObject> objects,std::span<const ShowcaseLight> lights){
    uint64_t h=1469598103934665603ull;
    for(const auto&o:objects)if(!o.movable){h=mix_hash(h,o.id);h=mix_hash(h,uint64_t(o.geometry));h=mix_hash(h,o.geometry_revision);h=mix_hash(h,uint64_t(o.material.surface));h=mix_hash(h,dot(o.material.emissive,o.material.emissive)>.001f);for(float f:o.transform.v)h=hash_float(h,f);for(float f:{o.material.base.x,o.material.base.y,o.material.base.z,o.material.metallic,o.material.roughness})h=hash_float(h,f);}
    for(const auto&l:lights)if(l.mobility==LightMobility::Static){h=mix_hash(h,light_transform_hash(l));for(float f:{l.color.x,l.color.y,l.color.z,l.intensity,l.temperature,l.tint})h=hash_float(h,f);h=mix_hash(h,uint64_t(l.units));h=mix_hash(h,l.use_temperature);}
    return h;
}
uint32_t LightingCpu::build_node(uint32_t first,uint32_t count){
    uint32_t index=uint32_t(nodes_.size());nodes_.push_back({});V3 lo{1e30f,1e30f,1e30f},hi{-1e30f,-1e30f,-1e30f};
    for(uint32_t n=first;n<first+count;++n){const auto&s=shapes_[static_indices_[n]];lo=minimum(lo,s.lo);hi=maximum(hi,s.hi);}nodes_[index].lo=lo;nodes_[index].hi=hi;
    if(count<=4){nodes_[index].first=first;nodes_[index].count=count;return index;}
    V3 extent=hi-lo;unsigned axis=extent.y>extent.x?1:0;if(extent.z>component(extent,axis))axis=2;uint32_t mid=first+count/2;
    std::nth_element(static_indices_.begin()+first,static_indices_.begin()+mid,static_indices_.begin()+first+count,[&](uint32_t a,uint32_t b){const auto&x=shapes_[a];const auto&y=shapes_[b];float xc=component(x.lo+x.hi,axis),yc=component(y.lo+y.hi,axis);return xc!=yc?xc<yc:x.id<y.id;});
    uint32_t left=build_node(first,mid-first),right=build_node(mid,first+count-mid);nodes_[index].left=left;nodes_[index].right=right;return index;
}
uint32_t LightingCpu::build_mesh_node(TriangleMesh&mesh,uint32_t first,uint32_t count){
    uint32_t index=uint32_t(mesh.nodes.size());mesh.nodes.push_back({});V3 lo{1e30f,1e30f,1e30f},hi{-1e30f,-1e30f,-1e30f};for(uint32_t n=first;n<first+count;++n){const auto&t=mesh.triangles[mesh.indices[n]];lo=minimum(lo,t.lo);hi=maximum(hi,t.hi);}mesh.nodes[index].lo=lo;mesh.nodes[index].hi=hi;
    if(count<=8){mesh.nodes[index].first=first;mesh.nodes[index].count=count;return index;}V3 extent=hi-lo;unsigned axis=extent.y>extent.x?1:0;if(extent.z>component(extent,axis))axis=2;uint32_t mid=first+count/2;
    std::nth_element(mesh.indices.begin()+first,mesh.indices.begin()+mid,mesh.indices.begin()+first+count,[&](uint32_t a,uint32_t b){float x=component(mesh.triangles[a].lo+mesh.triangles[a].hi,axis),y=component(mesh.triangles[b].lo+mesh.triangles[b].hi,axis);return x!=y?x<y:a<b;});uint32_t left=build_mesh_node(mesh,first,mid-first),right=build_mesh_node(mesh,mid,first+count-mid);mesh.nodes[index].left=left;mesh.nodes[index].right=right;return index;
}
bool LightingCpu::load_geometry(const char*root){
    constexpr const char*names[]={"modular_industrial_pipes_01_pipe01.kgmodel","power_box_01.kgmodel","Barrel_01.kgmodel"};std::array<TriangleMesh,3> loaded;uint64_t hash=1469598103934665603ull;
    for(size_t kind=0;kind<3;++kind){auto path=std::filesystem::path(root)/names[kind];FILE*f=std::fopen(path.string().c_str(),"rb");if(!f)return false;if(std::fseek(f,0,SEEK_END)){std::fclose(f);return false;}long size=std::ftell(f);if(size<long(sizeof(kgfx_model_file))||size>256*1024*1024){std::fclose(f);return false;}std::rewind(f);std::vector<unsigned char> data(static_cast<size_t>(size));bool ok=std::fread(data.data(),1,data.size(),f)==data.size();std::fclose(f);if(!ok)return false;kgfx_model_file header;std::memcpy(&header,data.data(),sizeof(header));if(std::memcmp(header.magic,"KGMODEL",7)||header.version!=KGFX_ASSET_VERSION||header.index_count%3||header.vertex_count>2000000||header.index_count>6000000||header.vertices_offset>data.size()||header.indices_offset>data.size()||uint64_t(header.vertex_count)*sizeof(kgfx_asset_vertex)>data.size()-header.vertices_offset||uint64_t(header.index_count)*4>data.size()-header.indices_offset)return false;
        for(unsigned char byte:data){hash^=byte;hash*=1099511628211ull;}V3 lo{header.bounds_min[0],header.bounds_min[1],header.bounds_min[2]},hi{header.bounds_max[0],header.bounds_max[1],header.bounds_max[2]},span=hi-lo,mid=(hi+lo)*.5f;M4 normalization=scale({1/std::max(span.x,.001f),1/std::max(span.y,.001f),1/std::max(span.z,.001f)})*translate(mid*-1);auto&mesh=loaded[kind];mesh.triangles.reserve(header.index_count/3);
        for(uint32_t i=0;i<header.index_count;i+=3){V3 p[3];for(unsigned n=0;n<3;++n){uint32_t index;std::memcpy(&index,data.data()+header.indices_offset+size_t(i+n)*4,4);if(index>=header.vertex_count)return false;kgfx_asset_vertex vertex;std::memcpy(&vertex,data.data()+header.vertices_offset+size_t(index)*sizeof(vertex),sizeof(vertex));p[n]=point(normalization,{vertex.position[0],vertex.position[1],vertex.position[2]});if(!std::isfinite(p[n].x)||!std::isfinite(p[n].y)||!std::isfinite(p[n].z))return false;}V3 normal=cross(p[1]-p[0],p[2]-p[0]);if(dot(normal,normal)<1e-20f)continue;mesh.triangles.push_back({p[0],p[1],p[2],unit(normal),minimum(minimum(p[0],p[1]),p[2]),maximum(maximum(p[0],p[1]),p[2])});}
        mesh.indices.resize(mesh.triangles.size());std::iota(mesh.indices.begin(),mesh.indices.end(),0u);mesh.nodes.reserve(mesh.triangles.size()*2);if(!mesh.indices.empty())build_mesh_node(mesh,0,uint32_t(mesh.indices.size()));
    }
    meshes_=std::move(loaded);mesh_hash_=hash;return true;
}
bool LightingCpu::intersect_mesh(uint8_t kind,V3 p,V3 d,float&distance,V3&normal) const{
    const auto&mesh=meshes_[kind-uint8_t(ShowcaseGeometry::Pipe)];bool found=false;std::array<uint32_t,64>stack{};uint32_t count=1;while(count){const auto&node=mesh.nodes[stack[--count]];if(!bounds(node.lo,node.hi,p,d,distance))continue;if(node.count){for(uint32_t i=node.first;i<node.first+node.count;++i){const auto&t=mesh.triangles[mesh.indices[i]];V3 e1=t.b-t.a,e2=t.c-t.a,q=cross(d,e2);float det=dot(e1,q);if(std::abs(det)<1e-10f)continue;float reciprocal=1/det;V3 delta=p-t.a;float u=dot(delta,q)*reciprocal;if(u<0||u>1)continue;V3 r=cross(delta,e1);float v=dot(d,r)*reciprocal;if(v<0||u+v>1)continue;float ray_distance=dot(e2,r)*reciprocal;if(ray_distance>.001f&&ray_distance<distance){distance=ray_distance;normal=t.normal;found=true;}}}else{stack[count++]=node.left;stack[count++]=node.right;}}return found;
}
void LightingCpu::intersect(const Shape&s,V3 origin,V3 direction,Hit&hit) const{
    if(!bounds(s.lo,s.hi,origin,direction,hit.distance))return;V3 p=point(s.inverse,origin),d=vector(s.inverse,direction);float distance=hit.distance;V3 normal{};
    if(s.kind>=uint8_t(ShowcaseGeometry::Pipe)&&!meshes_[s.kind-uint8_t(ShowcaseGeometry::Pipe)].triangles.empty()){
        if(!intersect_mesh(s.kind,p,d,distance,normal))return;
    }else if(s.kind==uint8_t(ShowcaseGeometry::Sphere)){
        float a=dot(d,d),b=dot(p,d),c=dot(p,p)-.25f,disc=b*b-a*c;if(disc<0)return;float t=(-b-std::sqrt(disc))/a;if(t<.001f)t=(-b+std::sqrt(disc))/a;if(t<.001f||t>=distance)return;distance=t;normal=unit(p+d*t);
    }else if(s.kind==uint8_t(ShowcaseGeometry::Cylinder)){
        float a=d.x*d.x+d.z*d.z,b=p.x*d.x+p.z*d.z,c=p.x*p.x+p.z*p.z-.25f,disc=b*b-a*c;
        if(a>1e-12f&&disc>=0)for(float sign:{-1.f,1.f}){float t=(-b+sign*std::sqrt(disc))/a;V3 q=p+d*t;if(t>.001f&&t<distance&&std::abs(q.y)<=.5f){distance=t;normal={q.x,0,q.z};}}
        if(std::abs(d.y)>1e-9f)for(float y:{-.5f,.5f}){float t=(y-p.y)/d.y;V3 q=p+d*t;if(t>.001f&&t<distance&&q.x*q.x+q.z*q.z<=.25f){distance=t;normal={0,y*2,0};}}
        if(distance==hit.distance)return;
    }else{
        float near=-1e30f,far=1e30f;V3 near_normal{},far_normal{};
        for(unsigned a=0;a<3;++a){float v=component(d,a),o=component(p,a);if(std::abs(v)<1e-9f){if(std::abs(o)>.5f)return;continue;}float t0=(-.5f-o)/v,t1=(.5f-o)/v;V3 n0=axes[a*2+1],n1=axes[a*2];if(t0>t1){std::swap(t0,t1);std::swap(n0,n1);}if(t0>near){near=t0;near_normal=n0;}if(t1<far){far=t1;far_normal=n1;}if(near>far)return;}
        if(near>.001f){distance=near;normal=near_normal;}else{distance=far;normal=far_normal;}if(distance<.001f||distance>=hit.distance)return;
    }
    normal=unit(vector(transpose(s.inverse),normal));if(dot(normal,direction)>0)normal=normal*-1;hit={origin+direction*distance,normal,s.albedo,distance,s.id,true};
}
LightingCpu::Hit LightingCpu::trace(V3 p,V3 d,float distance,bool static_geometry,bool moving_geometry) const{
    Hit hit;hit.distance=distance;
    if(static_geometry&&!nodes_.empty()){std::array<uint32_t,64> stack{};uint32_t count=1;while(count){const auto&node=nodes_[stack[--count]];if(!bounds(node.lo,node.hi,p,d,hit.distance))continue;if(node.count){for(uint32_t n=node.first;n<node.first+node.count;++n)intersect(shapes_[static_indices_[n]],p,d,hit);}else{stack[count++]=node.left;stack[count++]=node.right;}}}
    if(moving_geometry)for(uint32_t i:moving_indices_)intersect(shapes_[i],p,d,hit);return hit;
}
bool LightingCpu::occluded(V3 from,V3 to,bool moving) const{V3 delta=to-from;float distance=std::sqrt(dot(delta,delta));return distance>.002f&&trace(from,delta*(1/distance),distance-.002f,true,moving).found;}
float LightingCpu::transport(V3 position,V3 normal,const ShowcaseLight&l,bool moving) const{
    if(l.type==LightType::Sky){float visible=0;for(V3 d:axes)if(dot(normal,d)>0&&!trace(position+normal*.012f,d,80,true,moving).found)visible+=dot(normal,d);return visible*pi/3;}
    if(l.type==LightType::Directional){V3 direction=unit(l.direction)*-1;float cosine=std::max(0.f,dot(normal,direction));return cosine&&(!l.casts_shadow||!trace(position+normal*.012f,direction,100,true,moving).found)?cosine:0;}
    V3 delta=l.position-position;float distance2=dot(delta,delta);if(distance2>=l.radius*l.radius||distance2<.0001f)return 0;V3 direction=delta*(1/std::sqrt(distance2));float cosine=std::max(0.f,dot(normal,direction));
    float angular=1;if(l.type==LightType::Spot){float c=dot(direction*-1,l.direction);angular=std::clamp((c-l.spot_cosine)/std::max(.0001f,l.inner_cone_cosine-l.spot_cosine),0.f,1.f);angular=angular*angular*(3-2*angular);}if(l.type==LightType::Rect)angular=std::max(0.f,dot(direction*-1,l.direction));
    if(cosine*angular<=0||(l.casts_shadow&&occluded(position+normal*.012f,l.position,moving)))return 0;float edge=std::max(0.f,1-distance2*distance2/(l.radius*l.radius*l.radius*l.radius));return cosine*angular*edge*edge/std::max(distance2,l.source_radius*l.source_radius);
}
V3 LightingCpu::direct(V3 p,V3 n,std::span<const ShowcaseLight> lights,bool static_lights,bool moving) const{
    V3 result{};for(const auto&l:lights)if((l.mobility==LightMobility::Static)==static_lights){float intensity=light_intensity_candela(l);if(l.type==LightType::Rect&&static_lights){V3 tangent=unit(l.right),vertical=unit(cross(l.direction,tangent));for(float x:{-.375f,-.125f,.125f,.375f})for(float y:{-.375f,-.125f,.125f,.375f}){auto sample=l;sample.position=l.position+tangent*(l.width*x)+vertical*(l.height*y);result=result+light_color(l)*(intensity*transport(p,n,sample,moving)/16);}}else result=result+light_color(l)*(intensity*transport(p,n,l,moving));}return result;
}
V3 LightingCpu::bake_path(V3 p,V3 n,std::span<const ShowcaseLight> lights,uint32_t seed) const{
    V3 radiance{},throughput{1,1,1};for(uint32_t bounce=0;bounce<3;++bounce){V3 direction=hemisphere(n,seed);Hit hit=trace(p+n*.012f,direction,80,true,false);if(!hit.found)break;throughput=multiply(throughput,hit.albedo);radiance=radiance+multiply(throughput,direct(hit.position,hit.normal,lights,true,false));p=hit.position;n=hit.normal;}return radiance;
}
void LightingCpu::initialize(std::span<const ShowcaseObject> objects,std::span<const ShowcaseLight> lights,ProbeGrid grid,bool bake_static){
    grid_=grid;uint64_t count=uint64_t(grid.counts[0])*grid.counts[1]*grid.counts[2];if(count>max_probes)count=max_probes;
    shapes_.clear();static_indices_.clear();moving_indices_.clear();nodes_.clear();surfaces_.clear();shapes_.reserve(objects.size());static_indices_.reserve(objects.size());moving_indices_.reserve(objects.size());nodes_.reserve(objects.size()*2);surfaces_.reserve(objects.size());
    for(const auto&o:objects){if(o.material.surface!=ShowcaseSurface::Opaque||dot(o.material.emissive,o.material.emissive)>.001f)continue;Shape s;s.id=o.id;s.transform=o.transform;s.inverse=inverse(o.transform);s.kind=uint8_t(o.geometry);s.moving=o.movable;s.revision=o.geometry_revision;s.albedo={std::min(o.material.base.x,.9f)*(1-o.material.metallic),std::min(o.material.base.y,.9f)*(1-o.material.metallic),std::min(o.material.base.z,.9f)*(1-o.material.metallic)};s.lo={1e30f,1e30f,1e30f};s.hi={-1e30f,-1e30f,-1e30f};for(float x:{-.5f,.5f})for(float y:{-.5f,.5f})for(float z:{-.5f,.5f}){V3 p=point(s.transform,{x,y,z});s.lo=minimum(s.lo,p);s.hi=maximum(s.hi,p);}uint32_t index=uint32_t(shapes_.size());shapes_.push_back(s);(s.moving?moving_indices_:static_indices_).push_back(index);}
    if(!static_indices_.empty())build_node(0,uint32_t(static_indices_.size()));probes_.assign(size_t(count),{});rays_.assign(size_t(count)*rays_per_probe,{});probe_transport_hashes_.assign(size_t(count),{});probe_epochs_.assign(size_t(count),0);probe_priorities_.assign(size_t(count),0);
    for(uint32_t i=0;i<count;++i){uint32_t x=i%grid.counts[0],y=(i/grid.counts[0])%grid.counts[1],z=i/(grid.counts[0]*grid.counts[1]);auto&p=probes_[i];p.position=grid.origin+V3{float(x)*grid.spacing.x,float(y)*grid.spacing.y,float(z)*grid.spacing.z};for(uint32_t r=0;r<rays_per_probe;++r){auto&ray=rays_[i*rays_per_probe+r];float yy=1-2*(float(r)+.5f)/rays_per_probe,angle=float(r)*2.39996323f;ray.direction={std::sqrt(1-yy*yy)*std::cos(angle),yy,std::sqrt(1-yy*yy)*std::sin(angle)};ray.static_hit=trace(p.position,ray.direction,80,true,false);for(size_t l=0;l<std::min(lights.size(),size_t(24));++l)if(ray.static_hit.found&&lights[l].mobility==LightMobility::Stationary)ray.transport[l]=transport(ray.static_hit.position,ray.static_hit.normal,lights[l],false);for(unsigned a=0;a<6;++a){float w=std::max(0.f,dot(ray.direction,axes[a]))*4/rays_per_probe;float distance=std::min(ray.static_hit.distance,30.f);p.distance_moments[a].x+=distance*w;p.distance_moments[a].y+=distance*distance*w;if(!ray.static_hit.found)p.sky_visibility[a]+=w;}}}
    for(size_t l=0;l<std::min(lights.size(),size_t(24));++l)light_transforms_[l]=light_transform_hash(lights[l]);for(auto&hashes:probe_transport_hashes_)hashes=light_transforms_;metadata_={};metadata_.scene_hash=scene_hash(objects,lights);if(mesh_hash_)metadata_.scene_hash=mix_hash(metadata_.scene_hash,mesh_hash_);if(bake_static)bake(objects,lights);reset_dynamic();if(!workers_)workers_=std::make_unique<Workers>(this);
}
void LightingCpu::bake(std::span<const ShowcaseObject> objects,std::span<const ShowcaseLight> lights){
    for(const auto&l:lights)if(l.mobility==LightMobility::Static)++metadata_.static_light_count;
    for(size_t i=0;i<probes_.size();++i){auto&p=probes_[i];for(unsigned a=0;a<6;++a){V3 indirect{};for(unsigned s=0;s<12;++s)indirect=indirect+bake_path(p.position,axes[a],lights,uint32_t(i*288+a*12+s+1));indirect=indirect*(1.f/12);p.baked_lobes[a]=indirect;p.baked_indirect=p.baked_indirect+indirect*(1.f/6);p.baked_direct=p.baked_direct+direct(p.position,axes[a],lights,true,false)*(1.f/6);}}
    for(const auto&o:objects)if(!o.movable&&o.material.surface==ShowcaseSurface::Opaque){BakedSurface surface;surface.object_id=o.id;for(unsigned a=0;a<6;++a){V3 normal=unit(vector(transpose(inverse(o.transform)),axes[a])),position=point(o.transform,axes[a]*.503f);surface.direct[a]=direct(position,normal,lights,true,false);for(unsigned s=0;s<8;++s)surface.indirect[a]=surface.indirect[a]+bake_path(position,normal,lights,uint32_t(o.id*257+a*8+s+1))*(1.f/8);}surfaces_.push_back(surface);}metadata_.valid=true;
}
void LightingCpu::reset_dynamic(){cursor_=0;geometry_revision_=1;update_epoch_=0;std::fill(probe_epochs_.begin(),probe_epochs_.end(),0);std::fill(probe_priorities_.begin(),probe_priorities_.end(),0);light_values_={};for(auto&p:probes_){p.dynamic_indirect={};p.dynamic_lobes={};p.last_update=0;p.geometry_revision=0;}}
void LightingCpu::update(std::span<const ShowcaseObject> objects,std::span<const ShowcaseLight> lights,uint64_t frame){
    if(probes_.empty())return;
    for(uint32_t index:moving_indices_){auto&s=shapes_[index];for(const auto&o:objects)if(o.id==s.id&&o.geometry_revision!=s.revision){V3 old_center=(s.lo+s.hi)*.5f;s.transform=o.transform;s.inverse=inverse(o.transform);s.revision=o.geometry_revision;s.lo={1e30f,1e30f,1e30f};s.hi={-1e30f,-1e30f,-1e30f};for(float x:{-.5f,.5f})for(float y:{-.5f,.5f})for(float z:{-.5f,.5f}){V3 p=point(s.transform,{x,y,z});s.lo=minimum(s.lo,p);s.hi=maximum(s.hi,p);}++geometry_revision_;V3 center=(s.lo+s.hi)*.5f;for(size_t i=0;i<probes_.size();++i){V3 a=probes_[i].position-old_center,b=probes_[i].position-center;float distance=std::min(dot(a,a),dot(b,b));if(distance<36)probe_priorities_[i]=std::max(probe_priorities_[i],2/(1+distance));}break;}}
    for(size_t l=0;l<std::min(lights.size(),size_t(24));++l){const auto&light=lights[l];light_transforms_[l]=light_transform_hash(light);uint64_t values=light_transforms_[l];for(float value:{light.color.x,light.color.y,light.color.z,light.intensity,light.temperature,light.tint})values=hash_float(values,value);if(values!=light_values_[l]&&light.mobility!=LightMobility::Static){for(size_t i=0;i<probes_.size();++i){V3 delta=probes_[i].position-light.position;float distance2=dot(delta,delta);if(distance2<light.radius*light.radius)probe_priorities_[i]=std::max(probe_priorities_[i],4/(1+distance2));}}light_values_[l]=values;}
    // Reserve ceil(N/12) jobs for oldest probes, guaranteeing the deadline even
    // under constant motion. Spare jobs prioritize changed lighting/blockers.
    ++update_epoch_;uint32_t count=uint32_t(probes_.size()),jobs=std::min(probes_per_update,count),oldest=(count+11)/12;std::array<uint32_t,probes_per_update> selected{};std::array<bool,max_probes> used{};
    for(uint32_t job=0;job<jobs;++job){uint32_t chosen=0;double best=-1;for(uint32_t n=0;n<count;++n){uint32_t i=(cursor_+n)%count;if(used[i])continue;uint64_t age=update_epoch_-probe_epochs_[i];double score=job<oldest?double(age):double(probe_priorities_[i])+double(age)*.01;if(score>best){best=score;chosen=i;}}selected[job]=chosen;used[chosen]=true;probe_epochs_[chosen]=update_epoch_;probe_priorities_[chosen]=0;}
    cursor_=(selected[jobs-1]+1)%count;workers_->run(std::span(selected.data(),jobs),lights,frame);
}
void LightingCpu::update_probe(uint32_t i,std::span<const ShowcaseLight> lights,uint64_t frame){
    auto&p=probes_[i];p.dynamic_lobes={};p.dynamic_indirect={};p.distance_moments={};p.sky_visibility={};
        for(uint32_t r=0;r<rays_per_probe;++r){auto&ray=rays_[i*rays_per_probe+r];Hit hit=ray.static_hit;Hit moving=trace(p.position,ray.direction,hit.distance,false,true);if(moving.found)hit=moving;for(unsigned axis=0;axis<6;++axis){float w=std::max(0.f,dot(ray.direction,axes[axis]))*4/rays_per_probe;float distance=std::min(hit.distance,30.f);p.distance_moments[axis].x+=distance*w;p.distance_moments[axis].y+=distance*distance*w;if(!hit.found)p.sky_visibility[axis]+=w;}if(!hit.found)continue;V3 radiance{};
            for(size_t l=0;l<std::min(lights.size(),size_t(24));++l){const auto&light=lights[l];if(light.mobility==LightMobility::Static)continue;float coefficient;
                if(light.mobility==LightMobility::Stationary&&light.type!=LightType::Sky&&!moving.found){if(probe_transport_hashes_[i][l]!=light_transforms_[l])ray.transport[l]=transport(hit.position,hit.normal,light,false);coefficient=ray.transport[l];if(coefficient>0&&light.casts_shadow&&light.type!=LightType::Sky){V3 delta=light.type==LightType::Directional?unit(light.direction)*-100:light.position-hit.position;float length=std::sqrt(dot(delta,delta));if(trace(hit.position+hit.normal*.012f,delta*(1/length),length-.02f,false,true).found)coefficient=0;}}
                else coefficient=transport(hit.position,hit.normal,light,true);radiance=radiance+light_color(light)*(light_intensity_candela(light)*coefficient);
            }
            radiance=multiply(radiance,hit.albedo);for(unsigned a=0;a<6;++a)p.dynamic_lobes[a]=p.dynamic_lobes[a]+radiance*(std::max(0.f,dot(ray.direction,axes[a]))*4/rays_per_probe);
        }
        for(V3 l:p.dynamic_lobes)p.dynamic_indirect=p.dynamic_indirect+l*(1.f/6);p.last_update=frame;p.geometry_revision=geometry_revision_;probe_transport_hashes_[i]=light_transforms_;
}
LightingSample LightingCpu::sample(V3 position,V3 normal,uint64_t object_id) const{
    LightingSample result;for(const auto&s:surfaces_)if(s.object_id==object_id){float weights=0;for(unsigned a=0;a<6;++a){float w=std::max(0.f,dot(normal,axes[a]));w*=w;result.baked_direct=result.baked_direct+s.direct[a]*w;result.baked_indirect=result.baked_indirect+s.indirect[a]*w;weights+=w;}if(weights>0){result.baked_direct=result.baked_direct*(1/weights);result.baked_indirect=result.baked_indirect*(1/weights);}break;}
    std::array<std::pair<float,size_t>,8> nearest;nearest.fill({1e30f,0});for(size_t i=0;i<probes_.size();++i){V3 delta=position-probes_[i].position;float distance2=dot(delta,delta);for(size_t n=0;n<nearest.size();++n)if(distance2<nearest[n].first){for(size_t k=nearest.size()-1;k>n;--k)nearest[k]=nearest[k-1];nearest[n]={distance2,i};break;}}
    float sum=0;V3 indirect{},baked{};for(const auto&candidate:nearest){if(candidate.first>36)continue;const auto&p=probes_[candidate.second];float distance2=candidate.first;if(occluded(p.position,position+normal*.02f))continue;float w=1/std::max(distance2,.05f);float side=0;V3 dynamic{},stat{};for(unsigned a=0;a<6;++a){float v=std::max(0.f,dot(normal,axes[a]));v*=v;side+=v;dynamic=dynamic+p.dynamic_lobes[a]*v;stat=stat+p.baked_lobes[a]*v;}if(side>0){indirect=indirect+dynamic*(w/side);baked=baked+stat*(w/side);sum+=w;}}
    if(sum>0){result.dynamic_indirect=indirect*(1/sum);if(!object_id)result.baked_indirect=baked*(1/sum);}return result;
}
LightingSample LightingCpu::bake_irradiance(V3 p,V3 n,std::span<const ShowcaseLight> lights,uint32_t seed,uint32_t samples) const{
    LightingSample result;result.baked_direct=direct(p,n,lights,true,false);for(uint32_t i=0;i<samples;++i)result.baked_indirect=result.baked_indirect+bake_path(p,n,lights,seed+i*977u+1u)*(1.f/float(samples));return result;
}
namespace {
// Explicit scalar serialization: no compiler padding, pointers or native structs
// enter the v1 companion. The existing .kgmodel format remains unchanged.
bool write_u32(FILE*f,uint32_t v){unsigned char b[4]={uint8_t(v),uint8_t(v>>8),uint8_t(v>>16),uint8_t(v>>24)};return std::fwrite(b,1,4,f)==4;}
bool read_u32(FILE*f,uint32_t&v){unsigned char b[4];if(std::fread(b,1,4,f)!=4)return false;v=uint32_t(b[0])|(uint32_t(b[1])<<8)|(uint32_t(b[2])<<16)|(uint32_t(b[3])<<24);return true;}
bool write_v3(FILE*f,V3 v){return write_u32(f,float_bits(v.x))&&write_u32(f,float_bits(v.y))&&write_u32(f,float_bits(v.z));}
bool read_v3(FILE*f,V3&v){uint32_t x,y,z;if(!read_u32(f,x)||!read_u32(f,y)||!read_u32(f,z))return false;v={bits_float(x),bits_float(y),bits_float(z)};return std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z);}
}
bool LightingCpu::write_bake(const char*path) const{
    if(!metadata_.valid)return false;FILE*f=std::fopen(path,"wb");if(!f)return false;bool ok=std::fwrite("KGLIGHT1",1,8,f)==8&&write_u32(f,1)&&write_u32(f,uint32_t(metadata_.scene_hash))&&write_u32(f,uint32_t(metadata_.scene_hash>>32))&&write_u32(f,uint32_t(probes_.size()))&&write_u32(f,uint32_t(surfaces_.size()))&&write_u32(f,metadata_.static_light_count)&&write_u32(f,3);
    for(const auto&p:probes_){ok=ok&&write_v3(f,p.position)&&write_v3(f,p.baked_direct)&&write_v3(f,p.baked_indirect);for(V3 l:p.baked_lobes)ok=ok&&write_v3(f,l);}for(const auto&s:surfaces_){ok=ok&&write_u32(f,uint32_t(s.object_id))&&write_u32(f,uint32_t(s.object_id>>32));for(V3 v:s.direct)ok=ok&&write_v3(f,v);for(V3 v:s.indirect)ok=ok&&write_v3(f,v);}return std::fclose(f)==0&&ok;
}
bool LightingCpu::read_bake(const char*path,std::span<const ShowcaseObject>objects,std::span<const ShowcaseLight>lights){
    FILE*f=std::fopen(path,"rb");if(!f)return false;char magic[8];uint32_t version=0,low=0,high=0,probes=0,surfaces=0,statics=0,bounces=0;bool ok=std::fread(magic,1,8,f)==8&&!std::memcmp(magic,"KGLIGHT1",8)&&read_u32(f,version)&&read_u32(f,low)&&read_u32(f,high)&&read_u32(f,probes)&&read_u32(f,surfaces)&&read_u32(f,statics)&&read_u32(f,bounces);uint64_t hash=uint64_t(low)|(uint64_t(high)<<32);
    if(!ok||version!=1||hash!=(mesh_hash_?mix_hash(scene_hash(objects,lights),mesh_hash_):scene_hash(objects,lights))||probes!=probes_.size()||surfaces>objects.size()||bounces!=3){std::fclose(f);return false;}
    auto loaded_probes=probes_;std::vector<BakedSurface> loaded_surfaces(surfaces);
    for(auto&p:loaded_probes){V3 position;ok=ok&&read_v3(f,position)&&dot(position-p.position,position-p.position)<.000001f&&read_v3(f,p.baked_direct)&&read_v3(f,p.baked_indirect);for(V3&v:p.baked_lobes)ok=ok&&read_v3(f,v);}for(auto&s:loaded_surfaces){ok=ok&&read_u32(f,low)&&read_u32(f,high);s.object_id=uint64_t(low)|(uint64_t(high)<<32);for(V3&v:s.direct)ok=ok&&read_v3(f,v);for(V3&v:s.indirect)ok=ok&&read_v3(f,v);}ok=ok&&std::fgetc(f)==EOF;std::fclose(f);if(!ok)return false;probes_=std::move(loaded_probes);surfaces_=std::move(loaded_surfaces);metadata_={version,bounces,hash,statics,true};return true;
}
bool LightingCpu::read_lightmap(const char*path){
    if(!metadata_.valid)return false;FILE*f=std::fopen(path,"rb");if(!f)return false;char magic[8];uint32_t version=0,lo=0,hi=0,count=0,width=0,height=0;bool ok=std::fread(magic,1,8,f)==8&&!std::memcmp(magic,"KGLMAP1\0",8)&&read_u32(f,version)&&read_u32(f,lo)&&read_u32(f,hi)&&read_u32(f,width)&&read_u32(f,height)&&read_u32(f,count);
    if(!ok||version!=1||(uint64_t(lo)|(uint64_t(hi)<<32))!=metadata_.scene_hash||count>surfaces_.size()||width>4096||height>4096){std::fclose(f);return false;}
    std::vector<BakedUv> maps(count);for(auto&map:maps){ok=ok&&read_u32(f,lo)&&read_u32(f,hi);map.object_id=uint64_t(lo)|(uint64_t(hi)<<32);for(auto*array:{&map.u,&map.v})for(auto&v:*array){V3 xyz;uint32_t w=0;ok=ok&&read_v3(f,xyz)&&read_u32(f,w);v={xyz.x,xyz.y,xyz.z,bits_float(w)};ok=ok&&std::isfinite(v.w);}}ok=ok&&std::fgetc(f)==EOF;std::fclose(f);if(!ok)return false;lightmap_uv_=std::move(maps);return true;
}

}
