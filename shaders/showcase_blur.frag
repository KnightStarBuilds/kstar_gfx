#version 450
#extension GL_GOOGLE_include_directive : require
#define KGFX_FRAGMENT
#include "showcase_common.glsl"
layout(location=0) out vec4 out_color;
layout(binding=8) uniform sampler2D kgfx_t0;
layout(binding=18) uniform sampler2D kgfx_t10;
void main(){
    vec2 q=screen_uv();float depth=texture(kgfx_t10,q).r;vec3 total=vec3(0);float weights=0.0;
    for(int i=-3;i<=3;++i){
        vec2 s=q+post.xy*float(i);float weight=exp(-float(i*i)*0.18);
        weight*=exp(-abs(texture(kgfx_t10,s).r-depth)*2.0);
        total+=texture(kgfx_t0,s).rgb*weight;weights+=weight;
    }
    out_color=vec4(total/max(weights,0.001),1);
}
