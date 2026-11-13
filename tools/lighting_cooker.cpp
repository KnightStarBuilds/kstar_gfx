// SPDX-License-Identifier: MIT
// Shared authored geometry supplies the native, Blender-independent light bake.
#include "../src/demos/showcase_world.hpp"
#include "include/kgfx_asset_format.h"
#include <xatlas.h>
#include <algorithm>
#include <array>
#include <bit>
#include <cstdio>
#include <filesystem>
#include <stdexcept>
#include <vector>
namespace {
using namespace demo;
constexpr V3 axes[]={{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}};
V3 point(const M4&m,V3 p){return {m.v[0]*p.x+m.v[4]*p.y+m.v[8]*p.z+m.v[12],m.v[1]*p.x+m.v[5]*p.y+m.v[9]*p.z+m.v[13],m.v[2]*p.x+m.v[6]*p.y+m.v[10]*p.z+m.v[14]};}
V3 direction(const M4&m,V3 p){return point(m,p)-point(m,{});}
void require(bool value,const char*message){if(!value)throw std::runtime_error(message);}
void u32(FILE*f,uint32_t n){const unsigned char bytes[]={uint8_t(n),uint8_t(n>>8),uint8_t(n>>16),uint8_t(n>>24)};require(std::fwrite(bytes,1,4,f)==4,"Write failed");}
void v4(FILE*f,V4 v){for(float n:{v.x,v.y,v.z,v.w})u32(f,std::bit_cast<uint32_t>(n));}
struct Face {uint64_t object_id;uint32_t axis;std::array<V3,4> positions;V3 normal;};
float cross2(V2 a,V2 b){return a.x*b.y-a.y*b.x;}
V2 difference(V2 a,V2 b){return {a.x-b.x,a.y-b.y};}
void bake_atlas(const ShowcaseFrame&frame,const std::filesystem::path&directory){
    xatlas::Atlas*atlas=xatlas::Create();std::vector<Face> faces;std::vector<BakedUv> mappings;
    constexpr uint32_t indices[]={0,1,2,0,2,3};
    for(const auto&o:frame.objects){if(o.movable||o.geometry!=ShowcaseGeometry::Cube||o.material.surface!=ShowcaseSurface::Opaque)continue;
        mappings.push_back({o.id});
        for(uint32_t a=0;a<6;++a){V3 normal=axes[a],u=std::abs(normal.y)<.9f?V3{0,1,0}:V3{1,0,0},v=cross(normal,u);Face face;face.object_id=o.id;face.axis=a;face.normal=unit(direction(transpose(inverse(o.transform)),normal));
            face.positions={point(o.transform,(normal-u-v)*.5f),point(o.transform,(normal+u-v)*.5f),point(o.transform,(normal+u+v)*.5f),point(o.transform,(normal-u+v)*.5f)};
            xatlas::MeshDecl mesh;mesh.vertexCount=4;mesh.vertexPositionData=face.positions.data();mesh.vertexPositionStride=sizeof(V3);mesh.indexData=indices;mesh.indexCount=6;mesh.indexFormat=xatlas::IndexFormat::UInt32;
            require(xatlas::AddMesh(atlas,mesh)==xatlas::AddMeshError::Success,"xatlas rejected authored surface");faces.push_back(face);
        }
    }
    xatlas::PackOptions options;options.padding=2;options.bilinear=true;options.texelsPerUnit=4;options.resolution=0;options.bruteForce=true;options.rotateCharts=true;
    xatlas::Generate(atlas,{},options);require(atlas->width>0&&atlas->height>0&&atlas->width<=2048&&atlas->height<=2048,"xatlas resolution exceeds bake budget");
    uint32_t width=atlas->width,height=atlas->height;std::vector<V4> pixels(size_t(width)*height*12);std::vector<uint8_t> covered(size_t(width)*height,0);
    for(size_t n=0;n<faces.size();++n){const auto&face=faces[n];const auto&mesh=atlas->meshes[n];std::array<V2,4> uv{};
        for(uint32_t vertex=0;vertex<mesh.vertexCount;++vertex){const auto&v=mesh.vertexArray[vertex];require(v.atlasIndex==0,"Lightmap must use one packed atlas");uv[v.xref]={v.uv[0],v.uv[1]};}
        V3 edge_u=face.positions[1]-face.positions[0],edge_v=face.positions[3]-face.positions[0];float uu=dot(edge_u,edge_u),vv=dot(edge_v,edge_v),uvdot=dot(edge_u,edge_v),det=uu*vv-uvdot*uvdot;require(std::abs(det)>1e-15f,"Degenerate bake face");V3 dual_u=(edge_u*vv-edge_v*uvdot)*(1/det),dual_v=(edge_v*uu-edge_u*uvdot)*(1/det);
        V3 au=dual_u*((uv[1].x-uv[0].x)/width)+dual_v*((uv[3].x-uv[0].x)/width),av=dual_u*((uv[1].y-uv[0].y)/height)+dual_v*((uv[3].y-uv[0].y)/height);
        auto&mapping=mappings[n/6];mapping.u[face.axis]={au.x,au.y,au.z,uv[0].x/width-dot(au,face.positions[0])};mapping.v[face.axis]={av.x,av.y,av.z,uv[0].y/height-dot(av,face.positions[0])};
        for(unsigned triangle=0;triangle<2;++triangle){unsigned ids[]={0,triangle==0?1u:2u,triangle==0?2u:3u};V2 a=uv[ids[0]],b=uv[ids[1]],c=uv[ids[2]],ab=difference(b,a),ac=difference(c,a);float denominator=cross2(ab,ac);if(std::abs(denominator)<1e-9f)continue;
            int minx=std::max(0,int(std::floor(std::min({a.x,b.x,c.x})))),miny=std::max(0,int(std::floor(std::min({a.y,b.y,c.y})))),maxx=std::min(int(width)-1,int(std::ceil(std::max({a.x,b.x,c.x})))),maxy=std::min(int(height)-1,int(std::ceil(std::max({a.y,b.y,c.y}))));
            for(int y=miny;y<=maxy;++y)for(int x=minx;x<=maxx;++x){V2 p{float(x)+.5f,float(y)+.5f},ap=difference(p,a);float wb=cross2(ap,ac)/denominator,wc=cross2(ab,ap)/denominator,wa=1-wb-wc;if(wa<-.001f||wb<-.001f||wc<-.001f)continue;size_t texel=size_t(y)*width+uint32_t(x);if(covered[texel])continue;covered[texel]=1;V3 position=face.positions[ids[0]]*wa+face.positions[ids[1]]*wb+face.positions[ids[2]]*wc+face.normal*.016f;
                for(unsigned lobe=0;lobe<6;++lobe){auto light=frame.lighting->bake_irradiance(position,axes[lobe],frame.lights,uint32_t(texel*73856093u+lobe*19349663u+1),4);for(unsigned indirect=0;indirect<2;++indirect){unsigned tile=lobe+indirect*6;V3 value=indirect?light.baked_indirect:light.baked_direct;size_t offset=size_t(y+(tile/4)*height)*(width*4)+uint32_t(x)+(tile%4)*width;pixels[offset]={value.x,value.y,value.z,1};}}
            }
        }
    }
    // Two guard texels are filled from covered neighbours without crossing a
    // chart's packed padding. The sample mappings retain xatlas's half pixels.
    for(unsigned iteration=0;iteration<2;++iteration){auto old_coverage=covered;auto old_pixels=pixels;for(uint32_t y=0;y<height;++y)for(uint32_t x=0;x<width;++x){size_t texel=size_t(y)*width+x;if(old_coverage[texel])continue;int bestx=-1,besty=-1;for(int dy=-1;dy<=1&&bestx<0;++dy)for(int dx=-1;dx<=1;++dx){int xx=int(x)+dx,yy=int(y)+dy;if(xx<0||yy<0||xx>=int(width)||yy>=int(height))continue;if(old_coverage[size_t(yy)*width+uint32_t(xx)]){bestx=xx;besty=yy;break;}}if(bestx<0)continue;covered[texel]=1;for(unsigned tile=0;tile<12;++tile){size_t dest=size_t(y+(tile/4)*height)*(width*4)+x+(tile%4)*width,source=size_t(uint32_t(besty)+(tile/4)*height)*(width*4)+uint32_t(bestx)+(tile%4)*width;pixels[dest]=old_pixels[source];}}}
    const auto texture_path=directory/"industrial_lightmap.kgtex";FILE*f=std::fopen(texture_path.string().c_str(),"wb");require(f,"Cannot create baked lightmap texture");kgfx_texture_file header{};std::copy_n("KGTEX",5,header.magic);header.version=KGFX_ASSET_VERSION;header.width=width*4;header.height=height*3;header.mip_count=1;header.format=KGFX_ASSET_RGBA32F;header.srgb=0;header.data_offset=sizeof(header);header.data_bytes=pixels.size()*sizeof(V4);require(std::fwrite(&header,1,sizeof(header),f)==sizeof(header)&&std::fwrite(pixels.data(),sizeof(V4),pixels.size(),f)==pixels.size(),"Cannot write baked lightmap texture");require(std::fclose(f)==0,"Cannot finish lightmap texture");
    f=std::fopen((directory/"industrial.kglmap").string().c_str(),"wb");require(f,"Cannot create UV companion");require(std::fwrite("KGLMAP1\0",1,8,f)==8,"Cannot write UV header");u32(f,1);u32(f,uint32_t(frame.bake.scene_hash));u32(f,uint32_t(frame.bake.scene_hash>>32));u32(f,width);u32(f,height);u32(f,uint32_t(mappings.size()));for(const auto&m:mappings){u32(f,uint32_t(m.object_id));u32(f,uint32_t(m.object_id>>32));for(const auto*array:{&m.u,&m.v})for(V4 v:*array)v4(f,v);}require(std::fclose(f)==0,"Cannot finish UV companion");
    std::printf("Baked %zu Static cube receivers: xatlas %ux%u, twelve directional tiles, three diffuse bounces\n",mappings.size(),width,height);xatlas::Destroy(atlas);
}
}
int main(int argc,char**argv){try{if(argc!=3)throw std::runtime_error("Usage: kgfx_lighting_cooker <cooked-output-directory> <cooked-geometry-directory>");std::filesystem::path output=std::filesystem::path(argv[1])/"lighting";std::filesystem::create_directories(output);demo::ShowcaseWorld world("lighting_baked",1,0,false);require(world.load_geometry(argv[2],true),"Cannot load verified imported geometry for baking");auto frame=world.frame();require(frame.lighting->write_bake((output/"industrial.kglight").string().c_str()),"Cannot write probe companion bake");bake_atlas(frame,output);return 0;}catch(const std::exception&e){std::fprintf(stderr,"Lighting cook: %s\n",e.what());return 1;}}
