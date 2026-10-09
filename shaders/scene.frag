#version 450
layout(location=0) in vec3 world_position;
layout(location=1) in vec3 world_normal;
layout(location=2) in vec4 world_tangent;
layout(location=3) in vec2 uv;
layout(location=4) in vec4 shadow_position;
layout(location=5) in vec4 color;
layout(location=0) out vec4 out_color;
layout(std140,binding=0) uniform kgfx_u0 {
    mat4 model; mat4 normal_matrix; mat4 view_projection; mat4 light_projection; mat4 inverse_view_projection;
    vec4 camera; vec4 light_direction; vec4 light_color; vec4 base_color;
    vec4 material; vec4 options; vec4 emissive; vec4 environment;
};
layout(binding=8) uniform sampler2D kgfx_t0;
layout(binding=9) uniform sampler2D kgfx_t1;
layout(binding=10) uniform sampler2D kgfx_t2;
layout(binding=11) uniform sampler2D kgfx_t3;
layout(binding=12) uniform samplerCube kgfx_t4;
layout(binding=13) uniform samplerCube kgfx_t5;
layout(binding=14) uniform sampler2D kgfx_t6;
layout(binding=15) uniform sampler2D kgfx_t7;
const float PI=3.14159265359;
vec3 fresnel(float h,vec3 f0){return f0+(1.0-f0)*pow(clamp(1.0-h,0.0,1.0),5.0);}
float visibility(float nv,float nl,float rough){float a=rough*rough;float gv=nl*sqrt(nv*nv*(1.0-a*a)+a*a);float gl=nv*sqrt(nl*nl*(1.0-a*a)+a*a);return 0.5/max(gv+gl,0.00001);}
float shadow(vec3 n,vec3 l){
    vec3 p=shadow_position.xyz/shadow_position.w*0.5+0.5;
#ifdef KGFX_GNM
    // GNM render targets use top-left image rows; material UVs are unchanged.
    p.y=1.0-p.y;
#endif
    if(p.x<0.0||p.x>1.0||p.y<0.0||p.y>1.0||p.z>1.0||p.z<0.0)return 1.0;
    float bias=max(0.0015*(1.0-dot(n,l)),0.00055),result=0.0;
    vec2 d=1.0/vec2(textureSize(kgfx_t7,0));
    for(int y=-1;y<=1;++y)for(int x=-1;x<=1;++x)result+=(p.z-bias<=texture(kgfx_t7,p.xy+vec2(x,y)*d).r)?1.0:0.0;
    return result/9.0;
}
void main(){
    vec4 albedo=color*texture(kgfx_t0,uv);
    if(albedo.a<material.z)discard;
    vec3 n=normalize(world_normal);if(!gl_FrontFacing)n=-n;
    vec3 t=normalize(world_tangent.xyz-n*dot(n,world_tangent.xyz));
    vec3 b=cross(n,t)*world_tangent.w;
    vec3 tn=texture(kgfx_t1,uv).xyz*2.0-1.0;tn.xy*=material.w;
    n=normalize(mat3(t,b,n)*tn);
    vec3 orm=texture(kgfx_t2,uv).rgb;
    float rough=clamp(material.y*orm.g,0.045,1.0),metal=clamp(material.x*orm.b,0.0,1.0);
    vec3 v=normalize(camera.xyz-world_position),l=normalize(light_direction.xyz),h=normalize(v+l);
    float nv=max(dot(n,v),0.0001),nl=max(dot(n,l),0.0),nh=max(dot(n,h),0.0),vh=max(dot(v,h),0.0);
    vec3 f0=mix(vec3(0.04),albedo.rgb,metal),f=fresnel(vh,f0);
    float a=rough*rough,a2=a*a,den=nh*nh*(a2-1.0)+1.0;
    float distribution=a2/(PI*den*den);
    vec3 diffuse=(1.0-f)*(1.0-metal)*albedo.rgb/PI;
    vec3 radiance=(diffuse+distribution*visibility(nv,nl,rough)*f)*light_color.rgb*light_direction.w*nl*shadow(n,l);
    vec3 iblf=f0+(max(vec3(1.0-rough),f0)-f0)*pow(1.0-nv,5.0);
    vec3 irradiance=texture(kgfx_t4,n).rgb;
    vec3 reflection=textureLod(kgfx_t5,reflect(-v,n),rough*environment.x).rgb;
    vec2 brdf_uv=vec2(nv,rough);
#ifndef KGFX_GNM
    brdf_uv.y=1.0-brdf_uv.y;
#endif
    vec2 brdf=texture(kgfx_t6,brdf_uv).rg;
    float ao=mix(1.0,orm.r,options.w);
    radiance+=((1.0-iblf)*(1.0-metal)*albedo.rgb*irradiance+reflection*(iblf*brdf.x+brdf.y))*ao*environment.y;
    radiance+=texture(kgfx_t3,uv).rgb*emissive.rgb;
    if(options.x>0.5&&options.x<1.5)radiance=n*0.5+0.5;
    if(options.x>1.5&&options.x<2.5)radiance=albedo.rgb;
    if(options.x>2.5&&options.x<3.5)radiance=vec3(metal,rough,orm.r);
    if(options.x>3.5&&options.x<4.5)radiance=vec3(shadow(n,l));
    out_color=vec4(radiance,albedo.a);
}
