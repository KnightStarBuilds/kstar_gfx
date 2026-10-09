#version 450
layout(location=0) in vec2 uv;
layout(location=0) out vec4 out_color;
layout(std140,binding=0) uniform kgfx_u0 {
    mat4 model; mat4 normal_matrix; mat4 view_projection; mat4 light_projection; mat4 inverse_view_projection;
    vec4 camera; vec4 light_direction; vec4 light_color; vec4 base_color;
    vec4 material; vec4 options; vec4 emissive; vec4 environment;
};
layout(binding=13) uniform samplerCube kgfx_t5;
void main(){vec4 world=inverse_view_projection*vec4(uv*2.0-1.0,1,1);vec3 ray=normalize(world.xyz/world.w-camera.xyz);out_color=vec4(textureLod(kgfx_t5,ray,0).rgb*environment.y,1);}
