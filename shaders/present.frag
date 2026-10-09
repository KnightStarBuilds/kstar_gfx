#version 450
layout(location=0) in vec2 uv;
layout(location=0) out vec4 out_color;
layout(binding=8) uniform sampler2D kgfx_t0;
void main(){
    vec2 sample_uv=uv;
#ifdef KGFX_GNM
    sample_uv.y=1.0-sample_uv.y;
#endif
    out_color=texture(kgfx_t0,sample_uv);
}
