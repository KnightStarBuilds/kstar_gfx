#version 450
layout(location=0) in vec2 uv;
layout(std140,binding=0) uniform kgfx_u0 {
    mat4 model; mat4 normal_matrix; mat4 view_projection; mat4 light_projection; mat4 inverse_view_projection;
    vec4 camera; vec4 light_direction; vec4 light_color; vec4 base_color;
    vec4 material; vec4 options; vec4 emissive; vec4 environment;
};
layout(binding=8) uniform sampler2D kgfx_t0;
void main(){if(texture(kgfx_t0,uv).a*base_color.a<max(material.z,0.3))discard;}
