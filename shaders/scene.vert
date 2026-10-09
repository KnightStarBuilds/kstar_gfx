#version 450
layout(location=0) in vec3 position;
layout(location=1) in vec3 normal;
layout(location=2) in vec4 tangent;
layout(location=3) in vec2 texcoord;
layout(location=4) in vec4 instance_position_scale;
layout(location=5) in vec4 instance_color;
layout(std140,binding=0) uniform kgfx_u0 {
    mat4 model; mat4 normal_matrix; mat4 view_projection; mat4 light_projection; mat4 inverse_view_projection;
    vec4 camera; vec4 light_direction; vec4 light_color; vec4 base_color;
    vec4 material; vec4 options; vec4 emissive; vec4 environment;
};
layout(location=0) out vec3 world_position;
layout(location=1) out vec3 world_normal;
layout(location=2) out vec4 world_tangent;
layout(location=3) out vec2 uv;
layout(location=4) out vec4 shadow_position;
layout(location=5) out vec4 color;
void main() {
    vec3 p=position;
    color=base_color;
    if(options.z>0.5){p=p*instance_position_scale.w+instance_position_scale.xyz;color*=instance_color;}
    vec4 world=model*vec4(p,1);
    world_position=world.xyz;
    world_normal=normalize(mat3(normal_matrix)*normal);
    world_tangent=vec4(normalize(mat3(model)*tangent.xyz),tangent.w);
    uv=texcoord;
#ifndef KGFX_GNM
    // Uploaded 2D images have top-first asset UVs; GL texture storage is bottom-first.
    uv.y=1.0-uv.y;
#endif
    shadow_position=light_projection*world;
    gl_Position=view_projection*world;
}
