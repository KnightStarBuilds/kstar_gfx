#version 450
#extension GL_GOOGLE_include_directive : require
#define KGFX_FRAGMENT
#include "showcase_common.glsl"
layout(location=0) in vec3 world_position;
layout(location=1) in vec2 uv;
layout(location=2) in vec4 color;
layout(location=3) in vec4 effect;
layout(location=4) in vec3 local_illumination;
layout(location=0) out vec4 out_color;
layout(binding=8) uniform sampler2D kgfx_t0;
layout(binding=9) uniform sampler2D kgfx_t1;
layout(binding=10) uniform sampler2D kgfx_t2;
layout(binding=11) uniform sampler2D kgfx_t3;
layout(binding=12) uniform samplerCube kgfx_t4;
layout(binding=18) uniform sampler2D kgfx_t10;
layout(binding=20) uniform sampler2D kgfx_t12;
layout(binding=21) uniform sampler2D kgfx_t13;
layout(binding=14) uniform sampler2D kgfx_t6;
layout(binding=15) uniform sampler2D kgfx_t7;
layout(binding=16) uniform sampler2D kgfx_t8;
#include "showcase_volume_data.glsl"
void main(){
    vec2 q=screen_uv();float d=length(world_position-camera_time.xyz),near_d=texture(kgfx_t12,q).r,far_d=texture(kgfx_t13,q).r;
    int band=d>far_d?2:d>near_d?1:0;if(band!=int(params.w))discard;
    float opaque_d=texture(kgfx_t10,q).r;float soft=clamp((opaque_d-d)*3.0,0.0,1.0);
    int kind=int(effect.y+0.5);float frame_position=effect.x*15.0;int frame=min(int(frame_position),15),next_frame=min(frame+1,15);
    vec2 atlas=(vec2(float(frame%4),float(frame/4))+clamp(uv,vec2(0.006),vec2(0.994)))*0.25;
    vec2 next_atlas=(vec2(float(next_frame%4),float(next_frame/4))+clamp(uv,vec2(.006),vec2(.994)))*.25;
    vec2 single=uv;
#ifndef KGFX_GNM
    atlas.y=1.0-atlas.y;next_atlas.y=1.0-next_atlas.y;single.y=1.0-single.y;
#endif
    vec4 sprite;
    if(kind==2)sprite=mix(texture(kgfx_t0,atlas),texture(kgfx_t0,next_atlas),fract(frame_position));
    else if(kind==3||kind==4||kind==7)sprite=mix(texture(kgfx_t1,atlas),texture(kgfx_t1,next_atlas),fract(frame_position));
    else if(kind==0||kind==1)sprite=mix(texture(kgfx_t2,atlas),texture(kgfx_t2,next_atlas),fract(frame_position));
    else sprite=texture(kgfx_t3,single);
    if(kind==8){float radius=length(uv-0.5)*2.0;float ring=exp(-pow((radius-0.72)*13.0,2.0));sprite=vec4(vec3(0.5,0.7,0.8),ring);}
    float envelope=(kind==3||kind==4||kind==7)?smoothstep(0.0,.08,effect.x)*(1.0-smoothstep(.55,1.0,effect.x)):(kind==0||kind==1)?1.0-smoothstep(.8,1.0,effect.x):1.0;
    float alpha=sprite.a*color.a*soft*envelope;if(alpha<0.002)discard;
    vec3 radiance=sprite.rgb*color.rgb;
    if(kind==3||kind==4||kind==7){
        vec3 illumination=texture(kgfx_t4,vec3(0,1,0)).rgb*0.8+local_illumination;

        radiance*=illumination;
    }
    bool glow=kind==2||kind==5||kind==6;
    if(glow)radiance*=kind==2?4.0:7.0;
    radiance=apply_fog(radiance,world_position);
    // One premultiplied blend preserves mixed smoke/fire depth order. Emissive
    // particles use additive RGB with zero destination attenuation.
    float smoke_visibility=particle_smoke_visibility(world_position,band==0?0.0:band==1?near_d:far_d);
    out_color=vec4(radiance*alpha*smoke_visibility,glow?alpha*0.2:alpha);
}
