#version 450
#extension GL_GOOGLE_include_directive : require
#define KGFX_FRAGMENT
#include "showcase_common.glsl"
layout(location=0) out vec4 out_color;
layout(binding=16) uniform sampler2D kgfx_t8;
layout(binding=18) uniform sampler2D kgfx_t10;
#include "showcase_visibility.glsl"
void main(){
    vec2 q=screen_uv();float distance_to_surface=min(texture(kgfx_t10,q).r,45.0);vec3 ray=ray_at(q),result=vec3(0);
    for(int step_index=0;step_index<12;++step_index){float distance_along=(float(step_index)+.5)*distance_to_surface/12.0;vec3 p=camera_time.xyz+ray*distance_along;
        for(int i=0;i<8;++i){if(light_color[i].w<=0.0||light_control[i].w<=0.0)continue;float attenuation=light_attenuation(i,p);if(attenuation<.00001)continue;float visibility=local_shadow(i,p,-ray);result+=light_color[i].rgb*light_color[i].w*light_control[i].w*attenuation*visibility*exp(-distance_along*fog.x)*distance_to_surface*(fog.x*.08/12.0);}
    }
    out_color=vec4(result,1);
}
