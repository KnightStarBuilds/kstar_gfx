#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace demo {
struct GraphicsSettings {
    uint32_t width=1920,height=1080,samples=4,shadow_size=2048;
    uint32_t effects=0; // Quality=0, Performance=1, Cinematic=2.
    friend constexpr bool operator==(const GraphicsSettings&,const GraphicsSettings&)=default;
};
enum class GraphicsMode { quality,performance,cinematic,custom };
constexpr GraphicsSettings quality_settings(){return {};}
constexpr GraphicsSettings performance_settings(){return {1280,720,4,1024,1};}
constexpr GraphicsSettings cinematic_settings(){return {1920,1080,4,2048,2};}
constexpr GraphicsMode graphics_mode(const GraphicsSettings&s){
    return s==quality_settings()?GraphicsMode::quality:s==performance_settings()?GraphicsMode::performance:s==cinematic_settings()?GraphicsMode::cinematic:GraphicsMode::custom;
}
constexpr const char* mode_name(GraphicsMode mode){
    return mode==GraphicsMode::quality?"QUALITY":mode==GraphicsMode::performance?"PERFORMANCE":mode==GraphicsMode::cinematic?"CINEMATIC":"CUSTOM";
}
// Pending settings never affect the GPU until the host applies them between frames.
// Environment and inspection controls are staged with the render target settings.
struct GraphicsMenu {
    enum Row { mode,resolution,antialiasing,shadows,environment,exposure,diagnostic,scene,apply,back,quit,row_count };
    bool visible=false;
    Row row=mode;
    GraphicsSettings pending{};
    GraphicsMode pending_mode=GraphicsMode::quality;
    uint32_t pending_environment=0,pending_diagnostic=0;
    float pending_exposure=1;
    uint32_t pending_scene=0,scene_count=1,diagnostic_count=5;
    const char*status="LEFT / RIGHT TO EDIT. SELECT APPLY TO CONFIRM.";
    void open(const GraphicsSettings&s,uint32_t env,float exp,uint32_t diag){
        pending=s;pending_mode=graphics_mode(s);pending_environment=env;
        pending_exposure=exp;pending_diagnostic=diag;row=mode;visible=true;
        status="LEFT / RIGHT TO EDIT. SELECT APPLY TO CONFIRM.";
    }
    void move(int direction){row=Row((int(row)+direction+row_count)%row_count);}
    void adjust(int direction){
        if(!direction)return;
        switch(row){
        case mode:
            pending_mode=GraphicsMode((int(pending_mode)+direction+4)%4);
            if(pending_mode==GraphicsMode::quality)pending=quality_settings();
            if(pending_mode==GraphicsMode::performance)pending=performance_settings();
            if(pending_mode==GraphicsMode::cinematic)pending=cinematic_settings();
            break;
        case resolution:{
            constexpr uint32_t heights[]{720,900,1080};
            int index=pending.height==720?0:pending.height==900?1:2;
            index=(index+direction+3)%3;pending.height=heights[index];pending.width=pending.height*16/9;
            pending_mode=GraphicsMode::custom;break;
        }
        case antialiasing:pending.samples=pending.samples==4?1:4;pending_mode=GraphicsMode::custom;break;
        case shadows:pending.shadow_size=pending.shadow_size==2048?1024:2048;pending_mode=GraphicsMode::custom;break;
        case environment:pending_environment=1-pending_environment;break;
        case exposure:pending_exposure=std::clamp(pending_exposure*std::exp2(float(direction)*.25f),.001f,128.0f);break;
        case diagnostic:pending_diagnostic=uint32_t((int(pending_diagnostic)+direction+int(diagnostic_count))%int(diagnostic_count));break;
        case scene:pending_scene=uint32_t((int(pending_scene)+direction+int(scene_count))%int(scene_count));break;
        default:return;
        }
        status="PENDING CHANGES - SELECT APPLY TO CONFIRM.";
    }
};
}
