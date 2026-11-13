#include "demo.hpp"
#include <kstar_gfx/opengl.h>
#include <kgfx_scene_catalog.hpp>
#define SDL_MAIN_HANDLED
#include <SDL.h>
#include <cstdio>
#include <filesystem>
#include <iostream>
#include <stdexcept>

namespace {
struct Window {
    SDL_Window *window=nullptr;SDL_GLContext context=nullptr;bool fixed_resolution=false;
    uint32_t drawable_width=0,drawable_height=0;
    bool captured=false;
    ~Window(){if(context)SDL_GL_DeleteContext(context);if(window)SDL_DestroyWindow(window);SDL_Quit();}
};
void present(void*user){SDL_GL_SwapWindow(static_cast<Window*>(user)->window);}
void log(void*,const char*message){std::fprintf(stderr,"kstar_gfx: %s\n",message);}
void capture_pointer(void*user,bool capture){
    auto*w=static_cast<Window*>(user);
    capture=capture&&(SDL_GetWindowFlags(w->window)&SDL_WINDOW_INPUT_FOCUS);
    if(w->captured==capture)return;
    if(SDL_SetRelativeMouseMode(capture?SDL_TRUE:SDL_FALSE)==0)w->captured=capture;
}
bool pump(void*user,demo::Input&input){
    auto*w=static_cast<Window*>(user);SDL_Event event;
    input.presentation_width=w->drawable_width;input.presentation_height=w->drawable_height;
    while(SDL_PollEvent(&event)){
        if(event.type==SDL_QUIT)return false;
        if(event.type==SDL_KEYDOWN&&!event.key.repeat){switch(event.key.keysym.sym){case SDLK_ESCAPE:case SDLK_F1:input.menu_toggle=true;break;case SDLK_UP:input.menu_vertical=-1;break;case SDLK_DOWN:input.menu_vertical=1;break;case SDLK_LEFT:input.menu_horizontal=-1;break;case SDLK_RIGHT:input.menu_horizontal=1;break;case SDLK_RETURN:case SDLK_KP_ENTER:input.menu_accept=true;break;case SDLK_BACKSPACE:input.menu_back=true;break;case SDLK_e:input.next_environment=true;break;case SDLK_d:input.next_diagnostic=true;break;case SDLK_r:input.reset=true;break;case SDLK_LEFTBRACKET:input.exposure_stops=-.25f;break;case SDLK_RIGHTBRACKET:input.exposure_stops=.25f;break;default:break;}}
        if(event.type==SDL_KEYDOWN&&!event.key.repeat){switch(event.key.keysym.sym){
            case SDLK_f:input.interact=true;break;case SDLK_c:input.toggle_inspection=true;break;
            case SDLK_v:input.walkthrough_diagnostic=true;break;case SDLK_r:input.walk_reset=true;break;
            case SDLK_p:input.toggle_animation=true;break;case SDLK_l:input.cycle_light_color=true;break;
            case SDLK_m:input.matched_lighting=true;break;
            case SDLK_EQUALS:case SDLK_PLUS:case SDLK_KP_PLUS:input.light_intensity_delta=.25f;break;
            case SDLK_MINUS:case SDLK_KP_MINUS:input.light_intensity_delta=-.25f;break;
            case SDLK_PAGEUP:input.scene_delta=-1;break;case SDLK_PAGEDOWN:input.scene_delta=1;break;default:break;
        }}
        if(event.type==SDL_MOUSEMOTION){
            if(w->captured){input.look_x+=float(event.motion.xrel)*.0025f;input.look_y-=float(event.motion.yrel)*.0025f;}
            else if(event.motion.state&SDL_BUTTON_LMASK){int logical_w=0,logical_h=0;SDL_GetWindowSize(w->window,&logical_w,&logical_h);input.orbit_x-=float(event.motion.xrel)/float(logical_w>0?logical_w:1)*6.0f;input.orbit_y+=float(event.motion.yrel)/float(logical_h>0?logical_h:1)*3.0f;}
        }
        if(event.type==SDL_MOUSEWHEEL)input.zoom+=float(event.wheel.y);
        if(event.type==SDL_WINDOWEVENT&&(event.window.event==SDL_WINDOWEVENT_SIZE_CHANGED||event.window.event==SDL_WINDOWEVENT_RESTORED)){int x=0,y=0;SDL_GL_GetDrawableSize(w->window,&x,&y);if(x>0&&y>0){w->drawable_width=input.presentation_width=uint32_t(x);w->drawable_height=input.presentation_height=uint32_t(y);if(!w->fixed_resolution){input.drawable_width=uint32_t(x);input.drawable_height=uint32_t(y);}}}
    }
    input.focused=(SDL_GetWindowFlags(w->window)&SDL_WINDOW_INPUT_FOCUS)!=0;
    if(!input.focused)capture_pointer(user,false);
    if(input.focused){const auto*keys=SDL_GetKeyboardState(nullptr);input.move_x=float(keys[SDL_SCANCODE_D])-float(keys[SDL_SCANCODE_A]);input.move_z=float(keys[SDL_SCANCODE_W])-float(keys[SDL_SCANCODE_S]);input.sprint=keys[SDL_SCANCODE_LSHIFT]||keys[SDL_SCANCODE_RSHIFT];}
    return true;
}
void title(void*user,const char*text){SDL_SetWindowTitle(static_cast<Window*>(user)->window,text);}
}
int main(int argc,char**argv){
    try{
        auto options=demo::parse_options(argc,argv);if(options.help){std::cout<<demo::usage();return 0;}if(options.list_scenes){demo::print_scenes();return 0;}
        Window window;window.fixed_resolution=options.frames!=0||options.seconds>0||options.headless||kgfx_demo_catalog::find(options.scene)->walkthrough;SDL_SetMainReady();if(SDL_Init(SDL_INIT_VIDEO|SDL_INIT_EVENTS)!=0)throw std::runtime_error(SDL_GetError());
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION,4);SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION,1);SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,SDL_GL_CONTEXT_PROFILE_CORE);SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER,1);SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE,24);SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE,0);
#if defined(__APPLE__)
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS,SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);
#endif
        const uint32_t flags=SDL_WINDOW_OPENGL|SDL_WINDOW_RESIZABLE|SDL_WINDOW_ALLOW_HIGHDPI|(options.headless?SDL_WINDOW_HIDDEN:SDL_WINDOW_SHOWN);
        window.window=SDL_CreateWindow("kstar_gfx",SDL_WINDOWPOS_CENTERED,SDL_WINDOWPOS_CENTERED,int(options.width),int(options.height),flags);if(!window.window)throw std::runtime_error(SDL_GetError());window.context=SDL_GL_CreateContext(window.window);if(!window.context)throw std::runtime_error(SDL_GetError());
        if(SDL_GL_MakeCurrent(window.window,window.context)!=0)throw std::runtime_error(SDL_GetError());
        {int w=0,h=0;SDL_GL_GetDrawableSize(window.window,&w,&h);if(w>0&&h>0){window.drawable_width=uint32_t(w);window.drawable_height=uint32_t(h);}}
        SDL_GL_SetSwapInterval(options.benchmark||options.headless?0:1);
        if(options.resources.empty()){char*base=SDL_GetBasePath();if(!base)throw std::runtime_error("Cannot locate executable resources");options.resources=std::filesystem::path(base)/"resources";SDL_free(base);}
        // Bounded runs honor requested pixel dimensions on Retina. Interactive
        // windows render at their actual drawable resolution from the outset.
        if(!window.fixed_resolution){int w=0,h=0;SDL_GL_GetDrawableSize(window.window,&w,&h);if(w>0&&h>0){options.width=uint32_t(w);options.height=uint32_t(h);}}
        kgfx_gl_config gl{SDL_GL_GetProcAddress,present,&window};kgfx_device_desc desc{KGFX_ABI_VERSION,options.width,options.height,3,&gl,log,nullptr};kgfx_device*device=nullptr;
        auto result=kgfx_device_create(&desc,&device);if(result!=KGFX_OK)throw std::runtime_error("Graphics device creation failed: "+std::to_string(result));
        const demo::Host host{&window,pump,title,capture_pointer};int status=demo::run(device,options,host);capture_pointer(&window,false);kgfx_device_destroy(device);return status;
    }catch(const std::exception&e){std::cerr<<"kstar_gfx_demo: "<<e.what()<<'\n'<<demo::usage();return 1;}
}
