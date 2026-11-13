#version 450
#extension GL_GOOGLE_include_directive : require
#define KGFX_FRAGMENT
#include "showcase_common.glsl"
layout(location=0) out vec4 out_color;
layout(binding=18) uniform sampler2D kgfx_t10;
void main(){
    vec2 q=screen_uv();float d=texture(kgfx_t10,q).r;
    if(d>900.0){out_color=vec4(1);return;}
    vec3 p=camera_time.xyz+ray_at(q)*d;
    vec3 normal=normalize(cross(dFdx(p),dFdy(p)));
    if(dot(normal,camera_time.xyz-p)<0.0)normal=-normal;
    float obscurance=0.0;
    // Fixed rotated pattern avoids temporal noise and remains deterministic.
    for(int i=0;i<12;++i){
        float angle=float(i)*2.39996323;float radius=sqrt((float(i)+0.5)/12.0);
        vec2 s=q+vec2(cos(angle),sin(angle))*radius*min(0.09,0.72/max(d,1.0))*vec2(viewport.y/viewport.x,1);
        float sd=texture(kgfx_t10,clamp(s,vec2(0),vec2(1))).r;
        vec3 delta=camera_time.xyz+ray_at(s)*sd-p;float len=length(delta);
        obscurance+=max(dot(normal,delta/max(len,0.001))-0.06,0.0)*(1.0-smoothstep(0.15,1.3,len));
    }
    float ao=clamp(1.0-obscurance*1.4/12.0,0.35,1.0);out_color=vec4(ao,ao,ao,1);
}
