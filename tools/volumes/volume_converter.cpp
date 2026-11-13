// SPDX-License-Identifier: MIT
// Host-only conversion from CC0 OpenVDB simulations to portable 2D textures.
#include <openvdb/openvdb.h>
#include <openvdb/io/File.h>
#include <openvdb/tools/Interpolation.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>

namespace {
constexpr unsigned resolution=32,gutter=1,tile=resolution+2*gutter,frames=16,width=tile*resolution,height=tile*frames;
using Grid=openvdb::FloatGrid;
struct Frame {Grid::Ptr density,flame,temperature;};
Frame read(const std::filesystem::path& path,bool report=false){
    openvdb::io::File file(path.string());file.open();Frame frame;
    for(auto it=file.beginName();it!=file.endName();++it){
        auto base=file.readGrid(it.gridName());if(!base->isType<Grid>())continue;
        auto grid=openvdb::gridPtrCast<Grid>(base);std::string name=grid->getName();
        std::transform(name.begin(),name.end(),name.begin(),[](unsigned char c){return char(std::tolower(c));});
        if(report){auto b=grid->evalActiveVoxelBoundingBox();float maximum=0;for(auto v=grid->cbeginValueOn();v;++v)maximum=std::max(maximum,*v);std::cout<<name<<" bounds "<<b<<" max "<<maximum<<"\n";}
        if(name.find("density")!=std::string::npos)frame.density=grid;
        else if(name.find("flame")!=std::string::npos||name.find("fire")!=std::string::npos||name.find("heat")!=std::string::npos)frame.flame=grid;
        else if(name.find("temperature")!=std::string::npos)frame.temperature=grid;
    }
    file.close();if(!frame.density&&!frame.flame&&!frame.temperature)throw std::runtime_error("No density/flame/temperature grids: "+path.string());return frame;
}
float sample(const Grid::Ptr& grid,const openvdb::Vec3d& world){
    if(!grid)return 0;
    const auto p=grid->worldToIndex(world);
    return std::max(0.f,openvdb::tools::BoxSampler::sample(grid->tree(),p));
}
uint8_t byte(float v){return uint8_t(std::lround(std::clamp(v,0.f,1.f)*255));}
void u32(std::ofstream& out,uint32_t value){for(unsigned b=0;b<4;++b)out.put(char(value>>(b*8)));}
void u64(std::ofstream& out,uint64_t value){for(unsigned b=0;b<8;++b)out.put(char(value>>(b*8)));}
void texture(const std::filesystem::path& path,unsigned w,unsigned h,const std::vector<uint8_t>& pixels){
    std::ofstream out(path,std::ios::binary);out.write("KGTEX\0\0\0",8);for(auto v:{2u,w,h,1u,1u,0u})u32(out,v);u64(out,48);u64(out,pixels.size());out.write(reinterpret_cast<const char*>(pixels.data()),std::streamsize(pixels.size()));if(!out)throw std::runtime_error("Cannot write texture "+path.string());
}
}
int main(int argc,char**argv){try{
    openvdb::initialize();
    if(argc==3&&std::string_view(argv[1])=="--inspect"){read(argv[2],true);return 0;}
    if(argc!=int(frames)+2)throw std::runtime_error("Usage: kgfx_volume_converter output-directory frame0.vdb ... frame15.vdb (or --inspect file.vdb)");
    const std::filesystem::path output=argv[1];std::filesystem::create_directories(output);
    std::array<Frame,frames> source;
    openvdb::Vec3d lo(std::numeric_limits<double>::max()),hi(std::numeric_limits<double>::lowest());
    std::array<float,3> maxima{0,0,0};
    for(unsigned f=0;f<frames;++f){
        source[f]=read(argv[f+2]);unsigned c=0;
        for(const auto& grid:{source[f].density,source[f].flame,source[f].temperature}){
            if(grid){const auto bounds=grid->evalActiveVoxelBoundingBox();for(unsigned corner=0;corner<8;++corner){openvdb::Vec3d p;for(unsigned axis=0;axis<3;++axis)p[axis]=double((corner&(1u<<axis))?bounds.max()[axis]+1:bounds.min()[axis]);p=grid->indexToWorld(p);for(unsigned axis=0;axis<3;++axis){lo[axis]=std::min(lo[axis],p[axis]);hi[axis]=std::max(hi[axis],p[axis]);}}for(auto v=grid->cbeginValueOn();v;++v)maxima[c]=std::max(maxima[c],*v);}
            ++c;
        }
    }
    // JangaFX exports use Z-up. Preserve X and rotate source +Z to world +Y,
    // source +Y to world -Z, so handedness and the shared sequence bounds agree.
    const auto span=hi-lo;
    std::vector<uint8_t> atlas(size_t(width)*height*4),flipbook(size_t(128)*128*4);
    for(unsigned f=0;f<frames;++f){
        for(unsigned z=0;z<resolution;++z)for(unsigned y=0;y<tile;++y)for(unsigned x=0;x<tile;++x){
            const double px=std::clamp(int(x)-int(gutter),0,int(resolution)-1)/double(resolution-1),py=std::clamp(int(y)-int(gutter),0,int(resolution)-1)/double(resolution-1),pz=double(z)/(resolution-1);
            const openvdb::Vec3d p(lo.x()+px*span.x(),hi.y()-pz*span.y(),lo.z()+py*span.z());
            float d=sample(source[f].density,p)/std::max(.001f,maxima[0]),flame=sample(source[f].flame,p)/std::max(.001f,maxima[1]),temperature=sample(source[f].temperature,p)/std::max(.001f,maxima[2]);
            if(!source[f].density)d=std::max(flame,temperature*.45f);
            if(!source[f].flame&&source[f].temperature)flame=std::max(0.f,temperature-.15f)*1.17f;
            // Endpoint blend makes the atlas cyclic without a hard frame seam.
            if(f>=frames-3){const float t=float(f-(frames-4))/3.f,w=t*t*(3-2*t);const unsigned other=0;float od=sample(source[other].density,p)/std::max(.001f,maxima[0]),of=sample(source[other].flame,p)/std::max(.001f,maxima[1]),ot=sample(source[other].temperature,p)/std::max(.001f,maxima[2]);if(!source[other].density)od=std::max(of,ot*.45f);if(!source[other].flame&&source[other].temperature)of=std::max(0.f,ot-.15f)*1.17f;d+=(od-d)*w;flame+=(of-flame)*w;temperature+=(ot-temperature)*w;}
            const size_t i=(size_t(f*tile+y)*width+z*tile+x)*4;atlas[i]=byte(d);atlas[i+1]=byte(flame);atlas[i+2]=byte(temperature);atlas[i+3]=255;
        }
        // Distant flipbooks preserve column-integrated density and flame as
        // linear material data. Runtime still applies current lighting and
        // tint, and distributes extinction through the same depth segments.
        for(unsigned y=0;y<resolution;++y)for(unsigned x=0;x<resolution;++x){std::array<float,3> column{};for(unsigned z=0;z<resolution;++z){const size_t i=(size_t(f*tile+y+gutter)*width+z*tile+x+gutter)*4;for(unsigned c=0;c<3;++c)column[c]+=atlas[i+c]/(255.f*resolution);}const size_t i=(size_t((f/4)*resolution+y)*128+(f%4)*resolution+x)*4;for(unsigned c=0;c<3;++c)flipbook[i+c]=byte(column[c]);flipbook[i+3]=byte(1-std::exp(-column[0]*8));}
    }
    texture(output/"volume.kgtex",width,height,atlas);texture(output/"distant.kgtex",128,128,flipbook);
    std::ofstream meta(output/"volume.json");meta<<"{\n  \"schema\":1,\n  \"width\":"<<width<<",\n  \"height\":"<<height<<",\n  \"resolution\":32,\n  \"gutter\":1,\n  \"frames\":16,\n  \"fps\":16,\n  \"mips\":1,\n  \"channels\":\"density,flame,temperature,one\",\n  \"up\":\"Y\",\n  \"source_bounds\":["<<lo.x()<<","<<lo.y()<<","<<lo.z()<<","<<hi.x()<<","<<hi.y()<<","<<hi.z()<<"],\n  \"resident_bytes\":"<<atlas.size()+flipbook.size()<<"\n}\n";
    std::cout<<"Cooked "<<frames<<" VDB frames, "<<atlas.size()+flipbook.size()<<" resident bytes\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}}
