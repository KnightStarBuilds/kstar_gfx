#extension GL_GOOGLE_include_directive : require
#define KGFX_FRAGMENT
#include "showcase_common.glsl"
layout(location=0) in vec3 world_position;
layout(location=1) in vec3 world_normal;
layout(location=2) in vec4 world_tangent;
layout(location=3) in vec2 uv;
layout(location=0) out vec4 out_color;
layout(binding=8) uniform sampler2D kgfx_t0;
layout(binding=9) uniform sampler2D kgfx_t1;
layout(binding=10) uniform sampler2D kgfx_t2;
layout(binding=11) uniform sampler2D kgfx_t3;
layout(binding=12) uniform samplerCube kgfx_t4;
layout(binding=13) uniform samplerCube kgfx_t5;
layout(binding=14) uniform sampler2D kgfx_t6;
layout(binding=15) uniform sampler2D kgfx_t7;
layout(binding=16) uniform sampler2D kgfx_t8;
layout(binding=17) uniform sampler2D kgfx_t9;
layout(binding=18) uniform sampler2D kgfx_t10;
layout(binding=19) uniform sampler2D kgfx_t11;
layout(binding=20) uniform sampler2D kgfx_t12;
layout(binding=21) uniform sampler2D kgfx_t13;
#ifndef KGFX_OPAQUE
layout(binding=22) uniform sampler2D kgfx_t14;
layout(binding=23) uniform sampler2D kgfx_t15;
#endif
vec3 fresnel(float h,vec3 f0){return f0+(1.0-f0)*pow(clamp(1.0-h,0.0,1.0),5.0);}
vec3 brdf(vec3 albedo,vec3 n,vec3 v,vec3 l,float metal,float rough,float diffuse_weight){
    vec3 h=normalize(v+l);float nv=max(dot(n,v),0.0001),nl=max(dot(n,l),0.0),nh=max(dot(n,h),0.0);
    float a=rough*rough,a2=a*a,den=nh*nh*(a2-1.0)+1.0;
    vec3 f=fresnel(max(dot(v,h),0.0),mix(vec3(0.04),albedo,metal));
    float gv=nl*sqrt(nv*nv*(1.0-a2)+a2),gl=nv*sqrt(nl*nl*(1.0-a2)+a2);
    return ((1.0-f)*(1.0-metal)*albedo/PI*diffuse_weight+a2/(PI*den*den)*0.5/max(gv+gl,0.00001)*f)*nl;
}
#include "showcase_lighting.glsl"
#ifndef KGFX_OPAQUE
float selected_depth(vec2 q){return params.w>0.5?texture(kgfx_t13,q).r:texture(kgfx_t12,q).r;}
vec3 transmitted(vec2 q,vec2 offset,float rough,float d){
    vec2 s=q+offset;
    if(any(lessThan(s,vec2(0.002)))||any(greaterThan(s,vec2(0.998)))||texture(kgfx_t10,s).r<d+0.015||abs(selected_depth(s)-d)>max(0.15,d*0.025))s=q;
    vec3 c=texture(kgfx_t11,s).rgb;
    // Explicit taps are portable on GL 4.1 and do not need HDR mip generation.
    float radius=rough*rough*0.012;vec3 total=c;float weight=1.0;
    for(int i=0;i<4;++i){float a=float(i)*1.570796;vec2 t=s+vec2(cos(a),sin(a))*radius;
        if(all(greaterThan(t,vec2(0)))&&all(lessThan(t,vec2(1)))&&texture(kgfx_t10,t).r>d&&abs(selected_depth(t)-d)<max(0.15,d*0.025)){total+=texture(kgfx_t11,t).rgb;weight+=1.0;}}
    return total/weight;
}
#endif
void main(){
    if(dot(vec4(world_position,1),clip_plane)<0.0)discard;
    vec2 q=screen_uv();float distance_to_eye=length(world_position-camera_time.xyz);
#ifdef KGFX_OPAQUE
    const bool refractive=false,water=false;
#else
    bool refractive=surface.x>0.5,water=surface.x>1.5;
    if(refractive&&abs(selected_depth(q)-distance_to_eye)>max(0.005,distance_to_eye*0.0001))discard;
#endif
    vec2 coord=uv;
    if(object_options.x<2.5){
        // Authored structural surfaces tile every two metres along each face's
        // actual tangent axes. Vertical walls never inherit floor X/Z scales.
        vec3 authored_n=normalize(world_normal),authored_t=normalize(world_tangent.xyz);
        vec3 authored_b=cross(authored_n,authored_t)*world_tangent.w;
        coord=vec2(dot(world_position,authored_t),dot(world_position,authored_b))*0.5;
#ifndef KGFX_GNM
        coord.y=-coord.y;
#endif
    }
    vec4 albedo=base_color*texture(kgfx_t0,coord);
    vec3 n=normalize(world_normal);if((!gl_FrontFacing)!=(params.z>0.5))n=-n;
    vec3 t=normalize(world_tangent.xyz-n*dot(n,world_tangent.xyz));vec3 b=cross(n,t)*world_tangent.w;
    vec3 tn=texture(kgfx_t1,coord).xyz*2.0-1.0;tn.xy*=material.z;
    if(water){
        vec2 wuv=world_position.xz*0.35;
        vec3 n1=texture(kgfx_t1,wuv+vec2(camera_time.w*0.024,camera_time.w*0.011)).xyz*2.0-1.0;
        vec3 n2=texture(kgfx_t1,wuv*1.7-vec2(camera_time.w*0.014,camera_time.w*0.018)).xyz*2.0-1.0;
        tn=normalize(vec3((n1.xy+n2.xy)*0.38,1));
    }
    n=normalize(mat3(t,b,n)*tn);
    vec3 orm=texture(kgfx_t2,coord).rgb;
    float wet=material.w*texture(kgfx_t3,world_position.xz*0.2).r;
    float authored_rough=material.y*orm.g;
#ifdef KGFX_OPAQUE
    vec2 authored_uv=uv;
#ifndef KGFX_GNM
    authored_uv.y=1.0-authored_uv.y;
#endif
    if(mask_params.x>0.5){vec4 mask=surface_mask(kgfx_t12,authored_uv*mask_uv.xy+mask_uv.zw,int(mask_params.x));wet=max(wet,mask.r*mask_params.y);authored_rough=clamp(authored_rough+mask.g*mask_params.z,0.045,1.0);}
    for(int i=0;i<8;++i){if(float(i)>=mask_params.w)break;vec3 local=(decal_projection[i]*vec4(world_position,1)).xyz;if(any(greaterThan(abs(local),vec3(.5))))continue;
        vec4 mask=surface_mask(kgfx_t12,local.xz+.5,int(decal_params[i].x));float opacity=mask.a*decal_color[i].a;
        albedo.rgb=mix(albedo.rgb,albedo.rgb*decal_color[i].rgb,opacity);wet=max(wet,mask.r*decal_params[i].w);authored_rough=clamp(authored_rough+decal_params[i].z*mask.g*opacity,.045,1.0);
        n=normalize(n+(t+b)*(mask.b-.5)*decal_params[i].y*opacity);
    }
#endif
    float rough=clamp(mix(authored_rough,0.075,wet),0.045,1.0),metal=clamp(material.x*orm.b,0.0,1.0);
    albedo.rgb*=mix(1.0,0.66,wet);
    vec3 v=normalize(camera_time.xyz-world_position),l=normalize(sun_direction.xyz);
    float nv=max(dot(n,v),0.001);
    float sun_shadow=directional_shadow(world_position,n,l);
    vec3 lighting_albedo=refractive?vec3(0):albedo.rgb;
    float sun_diffuse=1.0;
#ifdef KGFX_OPAQUE
    if(cascade.z>.5&&object_options.y<.5)sun_diffuse=0.0;
#endif
    vec3 radiance=brdf(lighting_albedo,n,v,l,metal,rough,sun_diffuse)*sun_color.rgb*sun_direction.w*sun_shadow;
    for(int i=0;i<8;++i){
        if(light_color[i].w<=0.0)continue;
        vec3 delta=light_position[i].xyz-world_position;float d=length(delta),radius=light_position[i].w;
        vec3 direction=delta/max(d,0.001);float attenuation=light_attenuation(i,world_position);
        if(attenuation<=0.0||(light_shape[i].x<2.5&&dot(n,direction)<=0.0))continue;
        float visibility=local_shadow(i,world_position,n);
        float diffuse_weight=light_diffuse[i/4][i%4];
        if(light_shape[i].x>2.5&&light_shape[i].x<3.5)radiance+=rect_light(i,world_position,n,v,lighting_albedo,metal,rough,diffuse_weight)*visibility;
        else radiance+=brdf(lighting_albedo,n,v,direction,metal,rough,diffuse_weight)*light_color[i].rgb*light_color[i].w*attenuation*visibility;
    }
    vec3 baked=vec3(0),bounce=vec3(0);float coverage=0,sky_visibility=1.0;
#ifdef KGFX_OPAQUE
    vec3 direct_bake=vec3(0),indirect_bake=vec3(0);float sum=0;
    // Per-object fallback lobes follow the authored local faces. Atlas and
    // probe lobes below use world axes, independent of receiver rotation.
    vec3 fallback_n=vec3(dot(n,normalize(model[0].xyz)),dot(n,normalize(model[1].xyz)),dot(n,normalize(model[2].xyz)));
    for(int axis=0;axis<3;++axis){int face=axis*2+(fallback_n[axis]<0.0?1:0);float weight=fallback_n[axis]*fallback_n[axis];direct_bake+=baked_direct[face].rgb*weight;indirect_bake+=baked_indirect[face].rgb*weight;sum+=weight;}
    if(lightmap_options.x>.5){
        vec3 local_n=transpose(mat3(model))*world_normal,an=abs(local_n);int axis=an.x>=an.y&&an.x>=an.z?0:an.y>=an.z?1:2;int chart=axis*2+(local_n[axis]<0.0?1:0);
        vec2 chart_uv=clamp(vec2(dot(vec4(world_position,1),lightmap_u[chart]),dot(vec4(world_position,1),lightmap_v[chart])),vec2(0),vec2(1));direct_bake=vec3(0);indirect_bake=vec3(0);
        for(int a=0;a<3;++a){int face=a*2+(n[a]<0.0?1:0);float weight=n[a]*n[a];vec2 direct_uv=(chart_uv+vec2(float(face%4),float(face/4)))/vec2(4,3),indirect_uv=(chart_uv+vec2(float((face+6)%4),float((face+6)/4)))/vec2(4,3);
#ifndef KGFX_GNM
            direct_uv.y=1.0-direct_uv.y;indirect_uv.y=1.0-indirect_uv.y;
#endif
            direct_bake+=texture(kgfx_t13,direct_uv).rgb*weight;indirect_bake+=texture(kgfx_t13,indirect_uv).rgb*weight;
        }
    }
    vec3 probe_baked=vec3(0);sample_probes(kgfx_t11,world_position,n,probe_baked,bounce,sky_visibility,coverage);
    baked=direct_bake+indirect_bake;if(object_options.y>.5)baked=probe_baked;
    radiance+=lighting_albedo*(1.0-metal)*(baked+bounce)/PI;
#endif
    vec3 f0=mix(vec3(0.04),albedo.rgb,metal);vec3 iblf=f0+(max(vec3(1.0-rough),f0)-f0)*pow(1.0-nv,5.0);
    // Indoor probes contribute restrained fill; fixtures provide local contrast.
    vec3 reflection=textureLod(kgfx_t5,reflect(-v,n),rough*post.w).rgb*0.4;
    vec2 brdf_uv=vec2(nv,rough);
#ifndef KGFX_GNM
    brdf_uv.y=1.0-brdf_uv.y;
#endif
    vec2 envbrdf=texture(kgfx_t6,lookup_uv(vec2(nv,rough),0)).rg;
    float ao=orm.r*(params.z>0.5?1.0:bilateral_ao(q,distance_to_eye));
    float sky_weight=sky_color.w>.5&&object_options.y<.5?0.0:1.0;
    radiance+=lighting_albedo*(1.0-metal)*sky_color.rgb*clamp(sky_visibility,0.0,1.0)*ao*sky_weight/PI;
    radiance+=((1.0-iblf)*(1.0-metal)*lighting_albedo*texture(kgfx_t4,n).rgb*(probe_origin.w>.5?0.0:0.7)+reflection*(iblf*envbrdf.x+envbrdf.y))*ao;
    radiance+=emissive.rgb;
#ifndef KGFX_OPAQUE
    if(refractive){
        float interface_f0=pow((surface.y-1.0)/(surface.y+1.0),2.0);float f=interface_f0+(1.0-interface_f0)*pow(1.0-nv,5.0);
        vec2 distortion=vec2(dot(n,camera_right.xyz),dot(n,camera_up.xyz))*surface.w/max(distance_to_eye*0.15,1.0);
#ifdef KGFX_GNM
        distortion.y=-distortion.y;
#endif
        vec3 transmission=transmitted(q,distortion,rough,distance_to_eye);
        float thickness=surface.z/max(nv,0.15);
        if(water){
            thickness=clamp(texture(kgfx_t10,q).r-distance_to_eye,0.0,6.0);
            vec4 rp=reflection_projection*vec4(world_position,1);vec2 rq=image_uv(rp.xy/rp.w*0.5+0.5)+distortion;
            if(rp.w>0.0&&all(greaterThan(rq,vec2(0)))&&all(lessThan(rq,vec2(1))))reflection=texture(kgfx_t14,rq).rgb;
        }
        vec3 absorption=exp(-max(vec3(1)-tint.rgb,vec3(0))*thickness*(water?0.65:18.0));
        transmission=transmission*absorption+base_color.rgb*(1.0-absorption)*0.12;
        vec3 reflected=apply_fog(reflection,world_position);
        // Snapshot already contains fog behind the interface. Only local light
        // and reflection get foreground fog, avoiding double background fog.
        radiance=mix(transmission,reflected,f)+radiance*0.09;
        if(water){float impact_foam=texture(kgfx_t15,uv).r;float foam=max(impact_foam,(1.0-smoothstep(0.02,0.6,thickness))*.8)*emissive.w;radiance=mix(radiance,vec3(0.36,0.47,0.49),foam*0.45);}
    }else
#endif
    {radiance=apply_fog(radiance,world_position);}
    if(params.x>0.5&&params.x<1.5)radiance=n*0.5+0.5;
    if(params.x>1.5&&params.x<2.5)radiance=albedo.rgb;
    if(params.x>2.5&&params.x<3.5)radiance=vec3(metal,rough,ao);
    if(params.x>3.5&&params.x<4.5)radiance=vec3(sun_shadow);
    if(params.x>7.5&&params.x<8.5)radiance=baked;
    if(params.x>8.5&&params.x<9.5)radiance=bounce;
    if(params.x>9.5&&params.x<10.5)radiance=vec3(wet);
    if(params.x>10.5&&params.x<11.5)radiance=vec3(rough);
    if(params.x>11.5&&params.x<12.5)radiance=vec3(1.0-coverage,coverage,0);
    out_color=vec4(radiance,1);
}
