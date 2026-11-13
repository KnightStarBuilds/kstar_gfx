#version 450
#extension GL_GOOGLE_include_directive : require
#define KGFX_FRAGMENT
#include "showcase_common.glsl"
layout(location=0) in vec3 world_position;
layout(location=0) out float out_depth;
void main(){
    if(dot(vec4(world_position,1),clip_plane)<0.0)discard;
    out_depth=length(world_position-camera_time.xyz);
}
