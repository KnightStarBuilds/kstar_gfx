#version 450
#extension GL_GOOGLE_include_directive : require
#define KGFX_FRAGMENT
#include "showcase_common.glsl"
layout(location=0) out vec4 out_color;
layout(binding=13) uniform samplerCube kgfx_t5;
void main(){vec3 ray=ray_at(screen_uv());vec3 sky=textureLod(kgfx_t5,ray,0).rgb;out_color=vec4(mix(fog_color(),sky,0.7+max(ray.y,0.0)*0.3),1);}
