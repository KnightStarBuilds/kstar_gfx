#version 450
#extension GL_GOOGLE_include_directive : require
#define KGFX_FRAGMENT
#include "showcase_common.glsl"
layout(location=0) in vec3 world_position;
layout(location=0) out float out_depth;
layout(binding=18) uniform sampler2D kgfx_t10;
layout(binding=20) uniform sampler2D kgfx_t12;
void main(){
    vec2 q=screen_uv();float d=length(world_position-camera_time.xyz);
    if(d>=texture(kgfx_t10,q).r)discard;
    if(params.w>0.5&&d<=texture(kgfx_t12,q).r+0.004)discard;
    out_depth=d;
}
