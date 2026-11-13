// SPDX-License-Identifier: MIT
// Exercise scene ownership and menu transactions through the real shared loop.
#include "../src/demos/demo.hpp"
#include <kstar_gfx/opengl.h>
#include <kgfx_scene_catalog.hpp>
#define SDL_MAIN_HANDLED
#include <SDL.h>
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool value,const char*message){if(!value)throw std::runtime_error(message);}
void lighting_exposure_options(){
    for(const char*scene:{"lighting_baked","lighting_dynamic"}){
        std::array<char*,5> args{const_cast<char*>("demo"),const_cast<char*>("--demo"),const_cast<char*>(scene),const_cast<char*>("--exposure"),const_cast<char*>("0.27")};
        const auto standard=demo::parse_options(3,args.data()),explicit_value=demo::parse_options(5,args.data());
        require(std::abs(standard.exposure-.05f)<1e-6f,"Both physical-light scenes must share exposure 0.05");
        require(std::abs(explicit_value.exposure-.27f)<1e-6f,"Explicit exposure must override the lighting-scene default");
    }
}
struct Script {int kind=0,frame=0;};
struct PresentationProbe {
    Script*script=nullptr;
    std::array<std::array<unsigned char,4>,16>pixels{};
    using ReadBuffer=void(*)(unsigned);
    using ReadPixels=void(*)(int,int,int,int,unsigned,unsigned,void*);
    ReadBuffer read_buffer=nullptr;
    ReadPixels read_pixels=nullptr;
};
void present(void*user){
    auto&p=*static_cast<PresentationProbe*>(user);
    if(!p.script||p.script->kind!=6)return;
    // Inspect the actual presentation attachment, outside text and inside the
    // graphics panel. The triangle beneath is brightly coloured at this pixel.
    p.read_buffer(0x0405); // GL_BACK; the hidden native window is not swapped.
    p.read_pixels(25,17,1,1,0x1908,0x1401,p.pixels[size_t(p.script->frame-1)].data());
}
bool pump(void*user,demo::Input&input){
    auto&s=*static_cast<Script*>(user);++s.frame;
    if(s.kind==0&&s.frame>=2&&s.frame<=int(kgfx_demo_catalog::scenes.size())+1)input.scene_delta=1;
    if(s.kind==1){
        if(s.frame==1)input.menu_toggle=true;
        // 96x54 starts Custom; advance to Quality then Performance.
        if(s.frame==2||s.frame==3)input.menu_horizontal=1;
        if(s.frame>=4&&s.frame<=10)input.menu_vertical=1;
        if(s.frame==11)input.menu_horizontal=1; // Triangle -> Cubes.
        if(s.frame==12)input.menu_vertical=1;
        if(s.frame==13)input.menu_accept=true;
    }
    if(s.kind==2){
        if(s.frame==1)input.menu_toggle=true;
        if(s.frame==2)input.menu_horizontal=1;
        if(s.frame==3)input.menu_back=true;
    }
    if(s.kind==3&&s.frame==2)input.scene_delta=-1; // Missing boiler assets.
    if(s.kind==6&&(s.frame==1||s.frame==8))input.menu_toggle=true;
    if(s.kind==4||s.kind==5)input.focused=false; // Hidden/unfocused bounded captures.
    if(s.kind==7){
        if(s.frame==1){input.toggle_animation=true;input.cycle_light_color=true;input.light_intensity_delta=.5f;}
        if(s.frame==2)input.matched_lighting=true;
    }
    return true;
}
std::string read(const std::filesystem::path&p){std::ifstream in(p);return {std::istreambuf_iterator<char>(in),{}};}
}
int main(int argc,char**argv){
    SDL_Window*window=nullptr;SDL_GLContext context=nullptr;kgfx_device*device=nullptr;
    try{
        lighting_exposure_options();
        require(argc==3,"Expected packaged resources and scratch directory");
        const std::filesystem::path resources=std::filesystem::absolute(argv[1]),scratch=std::filesystem::absolute(argv[2]);
        std::filesystem::create_directories(scratch);
        SDL_SetMainReady();require(SDL_Init(SDL_INIT_VIDEO)==0,SDL_GetError());
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION,4);SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION,1);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,SDL_GL_CONTEXT_PROFILE_CORE);
#if defined(__APPLE__)
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS,SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);
#endif
        window=SDL_CreateWindow("scene session checks",0,0,96,54,SDL_WINDOW_OPENGL|SDL_WINDOW_HIDDEN);
        require(window,SDL_GetError());context=SDL_GL_CreateContext(window);require(context,SDL_GetError());
        PresentationProbe presentation;
        presentation.read_buffer=reinterpret_cast<PresentationProbe::ReadBuffer>(SDL_GL_GetProcAddress("glReadBuffer"));
        presentation.read_pixels=reinterpret_cast<PresentationProbe::ReadPixels>(SDL_GL_GetProcAddress("glReadPixels"));
        require(presentation.read_buffer&&presentation.read_pixels,"Cannot inspect presentation pixels");
        kgfx_gl_config gl{SDL_GL_GetProcAddress,present,&presentation};
        kgfx_device_desc desc{KGFX_ABI_VERSION,96,54,3,&gl,nullptr,nullptr};
        require(kgfx_device_create(&desc,&device)==KGFX_OK,"Cannot create graphics device");
        for(int kind=0;kind<8;++kind){
            demo::Options options;options.scene="triangle";options.width=96;options.height=54;options.samples=1;
            options.effects=0;options.headless=true;options.frames=16;options.capture_frame=16;options.resources=resources;
            options.output=scratch/("session "+std::to_string(kind)+".ppm");
            if(kind==4||kind==5)options.scene="waterworks";
            if(kind==4||kind==7)options.ticks_per_frame=2;
            if(kind==5||kind==6)options.headless=false;
            if(kind==7){options.scene="lighting_baked";options.view="glass";}
            if(kind==3){
                options.resources=scratch/"missing assets";
                std::filesystem::create_directories(options.resources/"shaders");
                for(const auto&entry:std::filesystem::directory_iterator(resources/"shaders"))
                    if(entry.is_regular_file())std::filesystem::copy_file(entry.path(),options.resources/"shaders"/entry.path().filename(),std::filesystem::copy_options::overwrite_existing);
            }
            Script script{kind};presentation.script=&script;const demo::Host host{&script,pump,nullptr,nullptr};
            require(demo::run(device,options,host)==0,"Scene session failed");
            auto metadata=options.output;metadata.replace_extension(".json");const auto json=read(metadata);
            if(kind==0){require(json.find("\"scene_switches\": "+std::to_string(kgfx_demo_catalog::scenes.size()))!=std::string::npos,"All catalog scenes must switch safely");require(json.find("\"demo\": \"triangle\"")!=std::string::npos,"Scene cycle must return to triangle");}
            if(kind==1){require(json.find("\"demo\": \"cubes\"")!=std::string::npos,"Apply must change the selected scene");require(json.find("\"width\": 1280, \"height\": 720")!=std::string::npos,"Applied Performance settings must survive scene replacement");}
            if(kind==2||kind==3){require(json.find("\"width\": 96, \"height\": 54")!=std::string::npos,"Cancel/load failure must retain active targets");require(json.find("\"scene_switches\": 0")!=std::string::npos,"Failed/cancelled transaction must not switch scenes");}
            if(kind==4||kind==5){require(json.find("\"scene_tick\": "+std::to_string(16*options.ticks_per_frame))!=std::string::npos,"Bounded capture must advance its selected fixed ticks without window focus");require(json.find("\"particles\": 0")==std::string::npos,"Unfocused captures must contain simulated particles");}
            if(kind==6){
                for(size_t frame=0;frame<presentation.pixels.size();++frame){
                    const auto&p=presentation.pixels[frame];
                    if(frame<7)require(p[0]>=4&&p[0]<=9&&p[1]>=6&&p[1]<=11&&p[2]>=10&&p[2]<=16,"Visible menu must persist across every dynamic buffer frame slot");
                    else require(p[0]>40,"Closing the menu must clear it from every presentation frame slot");
                }
            }
            if(kind==7){
                require(json.find("\"demo\": \"lighting_dynamic\"")!=std::string::npos,"Matched control must select the other lighting scene");
                require(json.find("\"view\": \"glass\"")!=std::string::npos,"Matched control must retain the selected view");
                require(json.find("\"animation_frozen\": true")!=std::string::npos,"Matched scene must retain animation freeze");
                require(json.find("\"animation_tick\": 0")!=std::string::npos,"Frozen animation must not advance while camera input remains active");
                require(json.find("\"light_color_variant\": 1")!=std::string::npos,"Stationary color control must survive the matched switch");
                require(json.find("\"stationary_intensity\": 1.5")!=std::string::npos,"Stationary intensity control must survive the matched switch");
            }
            require(json.find("\"cpu_frame_begin_ms\"")!=std::string::npos&&json.find("\"cpu_submission_ms\"")!=std::string::npos,"CPU recording must report separately from frame slot waits");
            require(json.find("\"cpu_total_frame_ms\"")!=std::string::npos&&json.find("\"cpu_simulation_ms\"")!=std::string::npos,"Total frame timing must include separately reported simulation/input");
            require(json.find("\"gpu_frame_ms\": {\"status\": ")!=std::string::npos,"GPU timing availability must be explicit");
            kgfx_device_wait_idle(device);kgfx_stats stats{};kgfx_get_stats(device,&stats);
            require(!stats.live_buffers&&!stats.live_textures&&!stats.live_pipelines&&!stats.live_samplers,"Session must release all owned GPU resources");
        }
        kgfx_device_destroy(device);device=nullptr;SDL_GL_DeleteContext(context);context=nullptr;SDL_DestroyWindow(window);window=nullptr;SDL_Quit();
        std::cout<<"Scene cycling, menu transactions, hidden-window simulation, visible menu persistence and resource retirement passed\n";return 0;
    }catch(const std::exception&e){
        std::cerr<<e.what()<<'\n';if(device)kgfx_device_destroy(device);if(context)SDL_GL_DeleteContext(context);if(window)SDL_DestroyWindow(window);SDL_Quit();return 1;
    }
}
