#version 450
layout(location=0) in vec3 position;
layout(location=3) in vec2 texcoord;
layout(location=4) in vec4 instance_position_scale;
layout(std140,binding=0) uniform kgfx_u0 {
    mat4 model; mat4 normal_matrix; mat4 view_projection; mat4 light_projection; mat4 inverse_view_projection;
    vec4 camera; vec4 light_direction; vec4 light_color; vec4 base_color;
    vec4 material; vec4 options; vec4 emissive; vec4 environment;
};
layout(location=0) out vec2 uv;
void main(){
    vec3 p=position;
    if(options.z>0.5)p=p*instance_position_scale.w+instance_position_scale.xyz;
    uv=texcoord;
#ifndef KGFX_GNM
    // Match the albedo sampling used by the main material pass.
    uv.y=1.0-uv.y;
#endif
    gl_Position=light_projection*model*vec4(p,1);
}
