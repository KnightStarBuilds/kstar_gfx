#version 450
#extension GL_GOOGLE_include_directive : require
#define KGFX_FRAGMENT
#include "showcase_common.glsl"
layout(location=0) out vec4 out_color;
layout(binding=8) uniform sampler2D kgfx_t0;
void main(){
    vec2 q=screen_uv();vec3 c=vec3(0);float weights=0;
    for(int i=-4;i<=4;++i){float w=exp(-float(i*i)*0.14);vec3 s=texture(kgfx_t0,q+post.xy*float(i)).rgb;
        if(post.z>0.5){float b=max(max(s.r,s.g),s.b);s*=max(b-1.0,0.0)/max(b,0.001);}
        c+=s*w;weights+=w;
    }
    out_color=vec4(c/weights,1);
}
