#pragma once
#include <kstar_gfx/gfx.h>
#include <cstdint>
#include <filesystem>
#include <string>

namespace demo {
struct Options {
    std::string scene="facility";
    std::string view="overview";
    std::string preset,sequence="idle";
    uint32_t width=1920,height=1080;
    uint32_t samples=4,shadow_size=2048;
    uint64_t frames=0,capture_frame=0;
    double seconds=0;
    bool headless=false,benchmark=false,help=false,list_scenes=false;
    std::filesystem::path resources,output;
    uint32_t environment=0,diagnostic=0;
    uint32_t seed=0,effects=2;
    uint32_t ticks_per_frame=1;
    float exposure=1.0f;
};
struct Input {
    float orbit_x=0,orbit_y=0,zoom=0,exposure_stops=0;
    bool next_environment=false,next_diagnostic=false,reset=false;
    bool menu_toggle=false,menu_accept=false,menu_back=false;
    int menu_vertical=0,menu_horizontal=0;
    float move_x=0,move_z=0,look_x=0,look_y=0;
    bool interact=false,toggle_inspection=false,walk_reset=false,sprint=false;
    bool walkthrough_diagnostic=false,focused=true;
    bool toggle_animation=false,cycle_light_color=false,matched_lighting=false;
    float light_intensity_delta=0;
    int scene_delta=0;
    uint32_t drawable_width=0,drawable_height=0;
    // Window pixels can differ from a bounded run's fixed render/capture size.
    uint32_t presentation_width=0,presentation_height=0;
};
struct Host {
    void *user=nullptr;
    bool (*pump)(void *,Input &)=nullptr;
    void (*title)(void *,const char *)=nullptr;
    // Host applies relative pointer mode only while walking with focus.
    void (*capture_pointer)(void *,bool)=nullptr;
};
Options parse_options(int argc,char **argv);
const char *usage();
void print_scenes();
// The host owns the device and native window. run releases all of its resources
// before returning; the same scene renderer is used by desktop and PS4 hosts.
int run(kgfx_device *,const Options &,const Host &host={});
}
