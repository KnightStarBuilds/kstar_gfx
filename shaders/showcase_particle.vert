#version 450
#extension GL_GOOGLE_include_directive : require
#include "showcase_common.glsl"
layout(location=0) in vec4 instance_illumination;
layout(location=4) in vec4 instance_position_size;
layout(location=5) in vec4 instance_color;
layout(location=6) in vec4 instance_velocity_kind;
layout(location=7) in vec4 instance_phase_rotation;
layout(location=0) out vec3 world_position;
layout(location=1) out vec2 uv;
layout(location=2) out vec4 color;
layout(location=3) out vec4 effect;
layout(location=4) out vec3 local_illumination;
void main(){
    int index=gl_VertexIndex%6;
    vec2 p=vec2((index==1||index==2||index==4)?1.0:0.0,(index==2||index==4||index==5)?1.0:0.0);
    uv=vec2(p.x,1.0-p.y);p-=0.5;
    float angle=instance_phase_rotation.y;vec2 rotated=mat2(cos(angle),sin(angle),-sin(angle),cos(angle))*p;
    int kind=int(instance_velocity_kind.w+0.5);
    vec3 right=camera_right.xyz,up=camera_up.xyz;
    float size=instance_position_size.w,stretch=1.0;
    if(kind==0||kind==1||kind==5){
        vec3 projected=instance_velocity_kind.xyz-dot(instance_velocity_kind.xyz,normalize(camera_time.xyz-instance_position_size.xyz))*normalize(camera_time.xyz-instance_position_size.xyz);
        if(length(projected)>0.01){up=normalize(projected);right=normalize(cross(up,normalize(camera_time.xyz-instance_position_size.xyz)));}
        stretch=kind==0?7.0:kind==5?4.0:2.0;
    }
    if(kind==8){right=vec3(1,0,0);up=vec3(0,0,1);}
    world_position=instance_position_size.xyz+(right*rotated.x+up*rotated.y*stretch)*size;
    local_illumination=instance_illumination.rgb;
    color=instance_color;effect=vec4(instance_phase_rotation.x,instance_velocity_kind.w,instance_phase_rotation.zw);
    gl_Position=view_projection*vec4(world_position,1);
}
