#version 450
#extension GL_GOOGLE_include_directive : require
#define KGFX_FRAGMENT
#include "showcase_common.glsl"
layout(location=0) out vec4 out_color;
layout(binding=8) uniform sampler2D kgfx_t0;
layout(binding=18) uniform sampler2D kgfx_t10;
layout(binding=20) uniform sampler2D kgfx_t12;
layout(binding=21) uniform sampler2D kgfx_t13;
void main(){
    vec2 q=screen_uv(),size=vec2(textureSize(kgfx_t0,0)),base_pixel=floor(q*size-.5),f=fract(q*size-.5);float depth=texture(kgfx_t10,q).r,near_depth=texture(kgfx_t12,q).r,far_depth=texture(kgfx_t13,q).r;vec4 total=vec4(0);float weight=0;
    for(int y=0;y<2;++y)for(int x=0;x<2;++x){vec2 s=(base_pixel+vec2(x,y)+.5)/size;float w=(x==0?1.0-f.x:f.x)*(y==0?1.0-f.y:f.y);w*=exp(-abs(texture(kgfx_t10,s).r-depth)*3.0-abs(texture(kgfx_t12,s).r-near_depth)*2.0-abs(texture(kgfx_t13,s).r-far_depth)*2.0);total+=texture(kgfx_t0,s)*w;weight+=w;}
    out_color=weight>.0001?total/weight:vec4(0);
}
