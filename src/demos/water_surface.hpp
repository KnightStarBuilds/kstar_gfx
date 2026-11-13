#pragma once
#include "math.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace demo {
// A fixed 64x64-cell pool. Simulation and collision use metres; the renderer
// samples this same interpolated surface for every depth and color pass.
// Storage is owned here so stepping, impacts and mesh extraction never allocate.
class WaterSurface {
public:
    static constexpr uint32_t cells=64,side=cells+1,vertex_count=side*side,index_count=cells*cells*6;
    static constexpr float fixed_step=1.f/60.f;
    struct Sample {float height=0;V3 normal{0,1,0};float foam=0;};
    struct Crown {V3 origin{};float radius=.1f,strength=0,age=0,lifetime=.55f;};
    static constexpr uint32_t max_crowns=16;
    void reset(V3 center,V2 extent){
        center_=center;extent_={std::max(3.f,extent.x),std::max(3.f,extent.y)};
        height_.fill(0);previous_.fill(0);velocity_.fill(0);next_.fill(0);foam_.fill(0);previous_foam_.fill(0);crowns_.fill({});crown_cursor_=0;tick_=0;
    }
    V3 center()const{return center_;}
    V2 extent()const{return extent_;}
    uint64_t tick()const{return tick_;}
    std::span<const Crown> crowns()const{return crowns_;}
    bool contains(V3 p)const{return std::abs(p.x-center_.x)<=extent_.x*.5f&&std::abs(p.z-center_.z)<=extent_.y*.5f;}
    // Impulses are vertical velocity (m/s); splashes deposit persistent foam.
    // Impacts outside the pool do not clamp onto its edge.
    bool impact(V3 p,float impulse,float radius=.16f){
        if(!contains(p)||!std::isfinite(impulse)||!std::isfinite(radius))return false;
        radius=std::clamp(radius,.06f,1.f);impulse=std::clamp(impulse,-3.f,3.f);
        if(std::abs(impulse)>.35f){p.y=sample(p).height;crowns_[crown_cursor_++%max_crowns]={p,radius,std::min(.28f,std::abs(impulse)*.09f),0,.55f};}
        const float gx=((p.x-center_.x)/extent_.x+.5f)*cells,gz=((p.z-center_.z)/extent_.y+.5f)*cells;
        const int rx=int(std::ceil(radius*cells/extent_.x)),rz=int(std::ceil(radius*cells/extent_.y));
        for(int z=std::max(0,int(gz)-rz);z<=std::min(int(cells),int(gz)+rz+1);++z)
            for(int x=std::max(0,int(gx)-rx);x<=std::min(int(cells),int(gx)+rx+1);++x){
                float dx=(float(x)-gx)*extent_.x/cells,dz=(float(z)-gz)*extent_.y/cells;
                float weight=std::max(0.f,1.f-(dx*dx+dz*dz)/(radius*radius));weight*=weight;
                const auto i=index(uint32_t(x),uint32_t(z));velocity_[i]=std::clamp(velocity_[i]+impulse*weight,-4.f,4.f);
                foam_[i]=std::clamp(foam_[i]+std::abs(impulse)*weight*.24f,0.f,1.f);
            }
        return true;
    }
    void step(){
        previous_=height_;previous_foam_=foam_;
        // Two bounded substeps satisfy the CFL limit even for the minimum
        // supported pool extent. Reflecting walls and drag dissipate impacts.
        constexpr float dt=fixed_step*.5f,speed2=1.44f;
        const float ix=float(cells*cells)/(extent_.x*extent_.x),iz=float(cells*cells)/(extent_.y*extent_.y);
        for(unsigned sub=0;sub<2;++sub){
            for(uint32_t z=0;z<side;++z)for(uint32_t x=0;x<side;++x){
                const auto i=index(x,z),l=index(x?x-1:x,z),r=index(x<cells?x+1:x,z),d=index(x,z?z-1:z),u=index(x,z<cells?z+1:z);
                const float lap=(height_[l]+height_[r]-2*height_[i])*ix+(height_[d]+height_[u]-2*height_[i])*iz;
                velocity_[i]=(velocity_[i]+speed2*lap*dt)*.991f;
                next_[i]=std::clamp(height_[i]+velocity_[i]*dt,-.22f,.22f);
            }
            height_.swap(next_);
        }
        // A sealed basin conserves its mean level: impact pressure displaces
        // water instead of slowly draining/filling the pool over a long replay.
        double mean_height=0,mean_velocity=0;
        for(size_t i=0;i<vertex_count;++i){mean_height+=height_[i];mean_velocity+=velocity_[i];}
        const float level=float(mean_height/vertex_count),drift=float(mean_velocity/vertex_count);
        for(size_t i=0;i<vertex_count;++i){height_[i]=std::clamp(height_[i]-level,-.22f,.22f);velocity_[i]-=drift;}
        for(uint32_t z=0;z<side;++z)for(uint32_t x=0;x<side;++x){
            const auto i=index(x,z),l=index(x?x-1:x,z),r=index(x<cells?x+1:x,z),d=index(x,z?z-1:z),u=index(x,z<cells?z+1:z);
            const float spread=(foam_[l]+foam_[r]+foam_[d]+foam_[u]-4*foam_[i])*.035f;
            next_[i]=std::clamp((foam_[i]+spread)*.996f+std::max(0.f,std::abs(velocity_[i])-.65f)*.006f,0.f,1.f);
        }
        foam_.swap(next_);for(auto& crown:crowns_)if(crown.strength>0){crown.age+=fixed_step;if(crown.age>=crown.lifetime)crown.strength=0;}++tick_;
    }
    Sample vertex(uint32_t x,uint32_t z,float alpha=1)const{
        x=std::min(x,cells);z=std::min(z,cells);alpha=std::clamp(alpha,0.f,1.f);
        const uint32_t l=x?x-1:x,r=x<cells?x+1:x,d=z?z-1:z,u=z<cells?z+1:z;
        const float dx=(height(r,z,alpha)-height(l,z,alpha))*cells/(float(r-l)*extent_.x);
        const float dz=(height(x,u,alpha)-height(x,d,alpha))*cells/(float(u-d)*extent_.y);
        const auto i=index(x,z);
        return {center_.y+height(x,z,alpha),unit({-dx,1,-dz}),previous_foam_[i]+(foam_[i]-previous_foam_[i])*alpha};
    }
    V3 position(uint32_t x,uint32_t z,float alpha=1)const{
        return {center_.x+(float(x)/cells-.5f)*extent_.x,vertex(x,z,alpha).height,center_.z+(float(z)/cells-.5f)*extent_.y};
    }
    Sample sample(V3 p,float alpha=1)const{
        const float gx=std::clamp(((p.x-center_.x)/extent_.x+.5f)*cells,0.f,float(cells)),gz=std::clamp(((p.z-center_.z)/extent_.y+.5f)*cells,0.f,float(cells));
        const uint32_t x=std::min(uint32_t(gx),cells-1),z=std::min(uint32_t(gz),cells-1);const float fx=gx-x,fz=gz-z;
        const Sample a=vertex(x,z,alpha),b=vertex(x+1,z,alpha),c=vertex(x,z+1,alpha),d=vertex(x+1,z+1,alpha);
        const float wa=(1-fx)*(1-fz),wb=fx*(1-fz),wc=(1-fx)*fz,wd=fx*fz;
        return {a.height*wa+b.height*wb+c.height*wc+d.height*wd,unit(a.normal*wa+b.normal*wb+c.normal*wc+d.normal*wd),a.foam*wa+b.foam*wb+c.foam*wc+d.foam*wd};
    }
private:
    V3 center_{};V2 extent_{6.4f,8};uint64_t tick_=0;
    std::array<float,vertex_count> height_{},previous_{},velocity_{},next_{},foam_{},previous_foam_{};
    std::array<Crown,max_crowns> crowns_{};uint32_t crown_cursor_=0;
    static constexpr size_t index(uint32_t x,uint32_t z){return size_t(z)*side+x;}
    float height(uint32_t x,uint32_t z,float alpha)const{const auto i=index(x,z);return previous_[i]+(height_[i]-previous_[i])*alpha;}
};
}
