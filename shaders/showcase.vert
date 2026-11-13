#version 450
#extension GL_GOOGLE_include_directive : require
#include "showcase_common.glsl"
layout(location=0) in vec3 position;
layout(location=1) in vec3 normal;
layout(location=2) in vec4 tangent;
layout(location=3) in vec2 texcoord;
layout(location=0) out vec3 world_position;
layout(location=1) out vec3 world_normal;
layout(location=2) out vec4 world_tangent;
layout(location=3) out vec2 uv;
void main(){
    vec4 p=model*vec4(position,1);
    // CPU water geometry is interpolated once and shared by every pass.
    world_position=p.xyz;
    world_normal=normalize(mat3(normal_matrix)*normal);
    world_tangent=vec4(normalize(mat3(model)*tangent.xyz),tangent.w);
    uv=texcoord;
#ifndef KGFX_GNM
    uv.y=1.0-uv.y;
#endif
    gl_Position=view_projection*p;
}
