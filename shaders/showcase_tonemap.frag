#version 450
#extension GL_GOOGLE_include_directive : require
#define KGFX_FRAGMENT
#include "showcase_common.glsl"
layout(location=0) out vec4 out_color;
layout(binding=8) uniform sampler2D kgfx_t0;
layout(binding=9) uniform sampler2D kgfx_t1;
layout(binding=10) uniform sampler2D kgfx_t2;
layout(binding=18) uniform sampler2D kgfx_t10;
layout(binding=20) uniform sampler2D kgfx_t12;
layout(binding=21) uniform sampler2D kgfx_t13;
layout(binding=22) uniform sampler2D kgfx_t14;
vec3 aces(vec3 x){return clamp((x*(2.51*x+0.03))/(x*(2.43*x+0.59)+0.14),0.0,1.0);}
vec3 srgb(vec3 x){return mix(12.92*x,1.055*pow(max(x,vec3(0)),vec3(1.0/2.4))-0.055,step(vec3(0.0031308),x));}
vec3 reconstructed_shafts(vec2 q){
    vec2 size=vec2(textureSize(kgfx_t2,0)),pixel=q*size-.5,base_pixel=floor(pixel),f=fract(pixel);float depth=texture(kgfx_t10,q).r,weight=0;vec3 sum=vec3(0);
    for(int y=0;y<2;++y)for(int x=0;x<2;++x){vec2 uv=(base_pixel+vec2(x,y)+.5)/size;float w=(x==0?1.0-f.x:f.x)*(y==0?1.0-f.y:f.y);w*=exp(-abs(texture(kgfx_t10,uv).r-depth)*2.0);sum+=texture(kgfx_t2,uv).rgb*w;weight+=w;}
    return weight>.0001?sum/weight:vec3(0);
}
void main(){
    vec2 q=screen_uv();vec3 c=texture(kgfx_t0,q).rgb;
    if(params.x<0.5)c+=texture(kgfx_t1,q).rgb*0.18+reconstructed_shafts(q);
    c*=params.y;
    // Subtle warm highlights and cool shadows, followed by one display encode.
    if(params.x<0.5)c*=mix(vec3(0.97,1.0,1.035),vec3(1.025,1.0,0.975),smoothstep(0.1,2.0,max(c.r,max(c.g,c.b))));
    c=srgb(aces(c));
    if(params.x>4.5&&params.x<5.5)c=vec3(exp(-texture(kgfx_t10,q).r*0.04));
    if(params.x>5.5&&params.x<6.5)c=texture(kgfx_t12,q).r>900.0?vec3(0.025):texture(kgfx_t13,q).r>900.0?vec3(0.05,0.65,0.45):vec3(0.15,0.45,1.0);
    if(params.x>6.5&&params.x<7.5&&texture(kgfx_t14,q).r<900.0)c=vec3(1.0,0.0,0.8);
    out_color=vec4(c,1);
}
