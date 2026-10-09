#pragma once
#include <kstar_gfx/gfx.h>
#include <cstdint>
#include <filesystem>
#include <string>

namespace demo {
struct Options {
    std::string scene="pbr";
    std::string view="overview";
    uint32_t width=1920,height=1080;
    uint64_t frames=0,capture_frame=0;
    double seconds=0;
    bool headless=false,benchmark=false,help=false;
    std::filesystem::path resources,output;
    uint32_t environment=0,diagnostic=0;
    float exposure=1.0f;
};
struct Input {
    float orbit_x=0,orbit_y=0,zoom=0,exposure_stops=0;
    bool next_environment=false,next_diagnostic=false,reset=false;
    uint32_t drawable_width=0,drawable_height=0;
    // Window pixels can differ from a bounded run's fixed render/capture size.
    uint32_t presentation_width=0,presentation_height=0;
};
struct Host {
    void *user=nullptr;
    bool (*pump)(void *,Input &)=nullptr;
    void (*title)(void *,const char *)=nullptr;
};
Options parse_options(int argc,char **argv);
const char *usage();
// The host owns the device and native window. run releases all of its resources
// before returning; the same scene renderer is used by desktop and PS4 hosts.
int run(kgfx_device *,const Options &,const Host &host={});
}
