// SPDX-License-Identifier: MIT
#pragma once
#include <array>
#include <cstdint>
#include <span>

namespace demo {
// A light owns all its faces for its lifetime. Colour and intensity do not enter
// the visibility key. The renderer maintains static/dynamic layers per slot.
class ShadowCache {
public:
    static constexpr unsigned capacity=32;
    struct Request {uint64_t id,visibility;unsigned faces;};
    struct Allocation {uint64_t id=0,visibility=0;unsigned first=0,faces=0;bool dirty=true;};
    void update(std::span<const Request> requests){
        for(auto&a:allocations_)if(a.id){
            bool keep=false;
            for(const auto&r:requests)if(r.id==a.id&&r.faces==a.faces){keep=true;break;}
            if(!keep)a={};
        }
        for(const auto&r:requests){
            if(!r.id||!r.faces||r.faces>capacity)continue;
            Allocation*existing=nullptr;
            for(auto&a:allocations_)if(a.id==r.id){existing=&a;break;}
            if(existing){existing->dirty=existing->dirty||existing->visibility!=r.visibility;existing->visibility=r.visibility;continue;}
            std::array<bool,capacity> used{};
            for(const auto&a:allocations_)if(a.id)for(unsigned i=0;i<a.faces;++i)used[a.first+i]=true;
            for(unsigned first=0;first+r.faces<=capacity;++first){
                bool fits=true;for(unsigned i=0;i<r.faces;++i)fits&=!used[first+i];
                if(!fits)continue;
                for(auto&a:allocations_)if(!a.id){a={r.id,r.visibility,first,r.faces,true};break;}
                break;
            }
        }
    }
    const Allocation*find(uint64_t id)const{for(const auto&a:allocations_)if(a.id==id&&id)return &a;return nullptr;}
    void rendered(){for(auto&a:allocations_)a.dirty=false;}
    void invalidate(){for(auto&a:allocations_)a.dirty=true;}
private:
    std::array<Allocation,capacity> allocations_{};
};
}
