// Shared atlas layout: 32 spatial slices (34px cells with one-texel gutters),
// sixteen temporal frames. No mips; separate distant flipbooks avoid slice bleed.
vec3 volume_texel(int asset,vec2 q){
#ifndef KGFX_GNM
    q.y=1.0-q.y;
#endif
    if(asset==0)return texture(kgfx_t6,q).rgb;
    if(asset==1)return texture(kgfx_t7,q).rgb;
    return texture(kgfx_t8,q).rgb;
}
vec3 volume_frame(int asset,vec3 p,int frame){
    float z=clamp(p.z,0.0,1.0)*31.0;float slice=floor(z);vec2 q=vec2(1.5)+clamp(p.xy,vec2(0),vec2(1))*31.0;
    vec2 first=(q+vec2(slice*34.0,float(frame)*34.0))/vec2(1088,544),second=(q+vec2(min(slice+1.0,31.0)*34.0,float(frame)*34.0))/vec2(1088,544);
    return mix(volume_texel(asset,first),volume_texel(asset,second),fract(z));
}
vec3 volume_sample(int i,vec3 world){
    vec3 p=(volume_inverse[i]*vec4(world,1)).xyz+.5;if(any(lessThan(p,vec3(0)))||any(greaterThan(p,vec3(1))))return vec3(0);
    float phase=mod(camera_time.w*16.0*volume_color[i].w+volume_params[i].w,16.0);int frame=int(floor(phase)),next=(frame+1)%16;
    return mix(volume_frame(int(volume_params[i].x),p,frame),volume_frame(int(volume_params[i].x),p,next),fract(phase));
}
#ifdef KGFX_VOLUME_RAYMARCH
vec3 volume_distant_texel(int asset,vec2 q){
#ifndef KGFX_GNM
    q.y=1.0-q.y;
#endif
    if(asset==0)return texture(kgfx_t3,q).rgb;
    if(asset==1)return texture(kgfx_t4,q).rgb;
    return texture(kgfx_t5,q).rgb;
}
vec3 volume_distant_frame(int asset,vec2 p,int frame){
    vec2 cell=vec2(float(frame%4),float(frame/4));
    return volume_distant_texel(asset,(cell*32.0+vec2(.5)+clamp(p,vec2(0),vec2(1))*31.0)/128.0);
}
vec3 volume_lod_sample(int i,vec3 world,vec3 ray){
    vec3 p=(volume_inverse[i]*vec4(world,1)).xyz+.5;if(any(lessThan(p,vec3(0)))||any(greaterThan(p,vec3(1))))return vec3(0);
    // The distant view stores mean column density, so overlapping density and
    // refractive segments keep the same Beer-Lambert integration. Oblique
    // views retain slices instead of exposing an edge-on billboard.
    vec3 local_ray=normalize((volume_inverse[i]*vec4(ray,0)).xyz);
    float weight=smoothstep(35.0,50.0,length(world-camera_time.xyz))*smoothstep(.55,.9,abs(local_ray.z));
    vec3 near_field=vec3(0);if(weight<.999)near_field=volume_sample(i,world);
    if(weight<.001)return near_field;
    float phase=mod(camera_time.w*16.0*volume_color[i].w+volume_params[i].w,16.0);int frame=int(floor(phase)),next=(frame+1)%16;
    vec3 far_field=mix(volume_distant_frame(int(volume_params[i].x),p.xy,frame),volume_distant_frame(int(volume_params[i].x),p.xy,next),fract(phase));
    return mix(near_field,far_field,weight);
}
#endif
vec2 volume_interval(int i,vec3 origin,vec3 direction){
    vec3 o=(volume_inverse[i]*vec4(origin,1)).xyz,d=(volume_inverse[i]*vec4(direction,0)).xyz;
    vec3 inv=sign(d+vec3(1e-10))/max(abs(d),vec3(1e-8));vec3 a=(-vec3(.5)-o)*inv,b=(vec3(.5)-o)*inv;
    vec3 lo=min(a,b),hi=max(a,b);return vec2(max(max(lo.x,lo.y),lo.z),min(min(hi.x,hi.y),hi.z));
}
float particle_smoke_visibility(vec3 world,float segment_start){
    if(volume_info.x<.5)return 1.0;vec3 ray=world-camera_time.xyz;float distance_to_particle=length(ray);ray/=max(distance_to_particle,.001);
    float start=distance_to_particle,end=segment_start;
    for(int i=0;i<4;++i){if(float(i)>=volume_info.x)break;vec2 interval=volume_interval(i,camera_time.xyz,ray);if(interval.y>max(interval.x,0.0)){start=min(start,max(interval.x,segment_start));end=max(end,min(interval.y,distance_to_particle));}}
    if(end<=start)return 1.0;float optical_depth=0.0,step_size=(end-start)/8.0;
    for(int step_index=0;step_index<8;++step_index){vec3 p=camera_time.xyz+ray*(start+(float(step_index)+.5)*step_size);for(int i=0;i<4;++i){if(float(i)>=volume_info.x)break;optical_depth+=volume_sample(i,p).r*volume_params[i].y*step_size;}}
    return exp(-optical_depth);
}
