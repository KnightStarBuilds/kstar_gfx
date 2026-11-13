// SPDX-License-Identifier: MIT
#include "../src/demos/shadow_cache.hpp"
#include <array>
#include <cstdio>
#include <cstdlib>

#define CHECK(expression) do { if(!(expression)) { \
    std::fprintf(stderr,"FAIL %s:%d: %s\n",__FILE__,__LINE__,#expression); \
    std::exit(1); } } while(0)

int main(){
    using Cache=demo::ShadowCache;
    Cache cache;
    // A point owns six atlas faces; the Rect hemisphere owns five. Reordering
    // lights or editing emission leaves their unchanged visibility cached.
    std::array<Cache::Request,3> lights{{{10,1,6},{20,1,5},{30,1,1}}};
    cache.update(lights);
    const unsigned point=cache.find(10)->first,rect=cache.find(20)->first,spot=cache.find(30)->first;
    CHECK(cache.find(10)->faces==6&&cache.find(20)->faces==5);
    cache.rendered();
    std::swap(lights[0],lights[2]);cache.update(lights);
    CHECK(cache.find(10)->first==point&&cache.find(20)->first==rect&&cache.find(30)->first==spot);
    CHECK(!cache.find(10)->dirty&&!cache.find(20)->dirty&&!cache.find(30)->dirty);
    lights[1].visibility=2;cache.update(lights);
    CHECK(cache.find(20)->dirty&&!cache.find(10)->dirty&&!cache.find(30)->dirty);
    // Repeated update before rendering must not discard pending invalidation.
    cache.update(lights);CHECK(cache.find(20)->dirty);
    cache.rendered();cache.invalidate();
    CHECK(cache.find(10)->dirty&&cache.find(20)->dirty&&cache.find(30)->dirty);

    Cache full;
    std::array<Cache::Request,7> crowded{{{1,1,6},{2,1,6},{3,1,6},{4,1,6},{5,1,5},{6,1,1},{7,1,5}}};
    full.update(crowded);
    CHECK(full.find(1)&&full.find(6)&&!full.find(7));
    for(const auto&r:crowded)if(const auto*a=full.find(r.id))CHECK(a->faces==r.faces);
    // Removing one light releases its complete allocation. Other lights stay
    // put, and a previously omitted whole light can acquire the vacated space.
    const auto survivor=*full.find(3);crowded[1]={};full.update(crowded);
    CHECK(!full.find(2)&&full.find(7)&&full.find(7)->faces==5);
    CHECK(full.find(3)->first==survivor.first);
    full.update({});for(unsigned id=1;id<=7;++id)CHECK(!full.find(id));

    std::array<Cache::Request,3> invalid{{{0,1,6},{1,1,0},{2,1,33}}};
    full.update(invalid);CHECK(!full.find(0)&&!full.find(1)&&!full.find(2));
    uint32_t random=71;
    std::array<Cache::Request,20> varying{};
    for(unsigned step=0;step<400;++step){
        for(unsigned i=0;i<varying.size();++i){
            random=random*1664525u+1013904223u;
            constexpr unsigned faces[]{1,5,6};
            varying[i]={random%5?uint64_t(i+1):0,random>>12,faces[(random>>8)%3]};
        }
        full.update(varying);
        std::array<bool,Cache::capacity> occupied{};
        for(const auto&r:varying)if(const auto*a=full.find(r.id)){
            CHECK(a->faces==r.faces&&a->first+a->faces<=Cache::capacity);
            for(unsigned face=0;face<a->faces;++face){CHECK(!occupied[a->first+face]);occupied[a->first+face]=true;}
        }
        full.rendered();
    }
    std::puts("PASS: persistent complete shadow allocations and visibility invalidation");
}
