#version 450
layout(location=0) in vec2 uv;
layout(location=0) out vec4 out_color;
layout(std140,binding=0) uniform kgfx_u0 {
    mat4 model; mat4 normal_matrix; mat4 view_projection; mat4 light_projection; mat4 inverse_view_projection;
    vec4 camera; vec4 light_direction; vec4 light_color; vec4 base_color;
    vec4 material; vec4 options; vec4 emissive; vec4 environment;
};
layout(binding=8) uniform sampler2D kgfx_t0;
vec3 aces(vec3 x){return clamp((x*(2.51*x+0.03))/(x*(2.43*x+0.59)+0.14),0.0,1.0);}
vec3 linear_to_srgb(vec3 x){return mix(x*12.92,1.055*pow(x,vec3(1.0/2.4))-0.055,step(vec3(0.0031308),x));}
void main(){
    vec2 sample_uv=uv;
#ifdef KGFX_GNM
    sample_uv.y=1.0-sample_uv.y;
#endif
    vec3 c=texture(kgfx_t0,sample_uv).rgb;
    out_color=vec4(linear_to_srgb(aces(c*camera.w)),1);
}
