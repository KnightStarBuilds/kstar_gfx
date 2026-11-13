#include "../src/demos/graphics_settings.hpp"
#include "../src/demos/overlay.hpp"
#include <cstdio>
#include <cstdlib>

namespace {
void require(bool condition,const char*message){if(!condition){std::fprintf(stderr,"%s\n",message);std::exit(1);}}
}
int main(){
    using namespace demo;
    constexpr auto quality=quality_settings(),performance=performance_settings();
    static_assert(quality==GraphicsSettings{1920,1080,4,2048});
    static_assert(performance==GraphicsSettings{1280,720,4,1024,1});
    static_assert(cinematic_settings()==GraphicsSettings{1920,1080,4,2048,2});
    GraphicsSettings active=quality;
    GraphicsMenu menu;
    menu.open(active,1,2,3);
    menu.adjust(1);
    require(menu.pending==performance,"Performance must preserve 4x MSAA while reducing internal resolution and shadow resolution");
    require(active==quality,"Editing must not mutate active graphics settings");
    menu.adjust(1);
    require(menu.pending==cinematic_settings(),"Cinematic must stage the full effects budget");
    // Dismissing/reopening discards pending GPU and inspection settings.
    menu.visible=false;menu.open(active,1,2,3);
    require(menu.pending==quality&&menu.pending_environment==1&&menu.pending_exposure==2&&menu.pending_diagnostic==3,"Reopening must restore applied settings");
    menu.move(-1);require(menu.row==GraphicsMenu::quit,"Up from the first row must reach Quit App");
    menu.move(1);require(menu.row==GraphicsMenu::mode,"Down from Quit App must return to the first row");
    menu.row=GraphicsMenu::resolution;menu.adjust(1);
    require(menu.pending==GraphicsSettings{1280,720,4,2048}&&menu.pending_mode==GraphicsMode::custom,"Resolution adjustment must preserve AA and shadows and select Custom");
    menu.adjust(1);require(menu.pending.width==1600&&menu.pending.height==900,"Custom 900p must preserve 16:9");
    menu.adjust(1);require(menu.pending.width==1920&&menu.pending.height==1080,"Custom resolution must wrap safely");
    menu.row=GraphicsMenu::antialiasing;menu.adjust(-1);require(menu.pending.samples==1,"AA off must select one raster sample");
    menu.adjust(1);require(menu.pending.samples==4,"AA on must restore four raster samples");
    menu.row=GraphicsMenu::shadows;menu.adjust(1);require(menu.pending.shadow_size==1024,"Custom shadows must switch to 1024");
    menu.row=GraphicsMenu::exposure;
    for(int n=0;n<100;++n)menu.adjust(-1);
    require(menu.pending_exposure==.001f,"Exposure must have a positive lower bound");
    for(int n=0;n<100;++n)menu.adjust(1);
    require(menu.pending_exposure==128,"Exposure must have a finite upper bound");
    menu.row=GraphicsMenu::diagnostic;menu.pending_diagnostic=0;menu.adjust(-1);
    require(menu.pending_diagnostic==4,"Diagnostic reverse navigation must wrap without unsigned overflow");
    menu.row=GraphicsMenu::environment;menu.pending_environment=0;menu.adjust(-1);
    require(menu.pending_environment==1,"Either direction must switch to the other HDRI");
    menu.row=GraphicsMenu::mode;menu.pending_mode=GraphicsMode::custom;menu.adjust(1);
    require(menu.pending==quality&&menu.pending_mode==GraphicsMode::quality,"Quality must restore the complete preset after custom edits");
    menu.scene_count=10;menu.row=GraphicsMenu::scene;menu.pending_scene=0;menu.adjust(-1);
    require(menu.pending_scene==9,"Scene selection must wrap without changing active state");
    // Every symbol in the exposure control hint must remain visible.
    require(Overlay::glyph('[')!=Overlay::glyph(' ')&&Overlay::glyph(']')!=Overlay::glyph(' '),"Desktop exposure keys must have visible glyphs");
    // Exercise a conservative upper bound of visible menu text without growth.
    Overlay overlay;const auto capacity=overlay.vertices.capacity();
    for(int row=0;row<26;++row)overlay.text(48,float(row)*32,"GRAPHICS QUALITY PERFORMANCE SHADOWS 0123456789",{1,1,1});
    require(overlay.vertices.size()<Overlay::capacity&&overlay.vertices.capacity()==capacity,"A full overlay must fit the fixed vertex arena");
    std::puts("Graphics settings/menu checks passed");
}
