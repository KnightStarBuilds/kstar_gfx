#version 450
#extension GL_GOOGLE_include_directive : require
#include "showcase_common.glsl"
layout(location=0) in vec3 position;
layout(location=4) in vec4 instance_x;
layout(location=5) in vec4 instance_y;
layout(location=6) in vec4 instance_z;
layout(location=7) in vec4 instance_origin;
layout(location=0) out vec3 world_position;
void main(){
    mat4 transform=mat4(instance_x,instance_y,instance_z,instance_origin);
    vec4 world=transform*vec4(position,1);
    world_position=world.xyz;
    gl_Position=view_projection*world;
}
