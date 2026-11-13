#ifndef KGFX_LOCAL_SHADOW_TEXTURE
#define KGFX_LOCAL_SHADOW_TEXTURE kgfx_t8
#endif
// The local atlas has 32 persistent face slots, with static visibility in its
// upper half and moving-occluder visibility in its lower half.
vec2 local_tile_uv(int tile,vec2 p){
    vec2 origin=vec2(float(tile%8),float(tile/8));
#ifndef KGFX_GNM
    origin.y=7.0-origin.y;
#endif
    vec2 texel=1.0/vec2(textureSize(KGFX_LOCAL_SHADOW_TEXTURE,0));
    return origin/8.0+texel*2.0+image_uv(p)*(vec2(.125)-texel*4.0);
}
float face_visibility(int slot,vec3 world,float bias,float softness){
    vec4 c=spot_projection[slot]*vec4(world,1);if(c.w<=0.0)return 1.0;
    vec3 p=c.xyz/c.w*.5+.5;
    if(any(lessThan(p,vec3(0)))||any(greaterThan(p,vec3(1))))return 1.0;
    vec2 texel=1.0/vec2(textureSize(KGFX_LOCAL_SHADOW_TEXTURE,0)),a=local_tile_uv(slot,p.xy),b=local_tile_uv(slot+32,p.xy);
    vec2 guard=texel*2.5;vec2 alo=floor(a*8.0)/8.0+guard,ahi=floor(a*8.0)/8.0+vec2(.125)-guard,blo=floor(b*8.0)/8.0+guard,bhi=floor(b*8.0)/8.0+vec2(.125)-guard;
    float result=0.0;
    // Keep taps within the guard band. Rect visibility is explicitly sampled
    // from the emitter centre; surface integration is used for illumination.
    for(int y=0;y<2;++y)for(int x=0;x<2;++x){vec2 tap=(vec2(x,y)-.5)*1.5*texel*clamp(softness,0.0,1.5);float d=min(texture(KGFX_LOCAL_SHADOW_TEXTURE,clamp(a+tap,alo,ahi)).r,texture(KGFX_LOCAL_SHADOW_TEXTURE,clamp(b+tap,blo,bhi)).r);result+=p.z-bias<=d?1.0:0.0;}
    return result*.25;
}
int local_face(int i,vec3 world){
    vec3 d=world-light_position[i].xyz;
    if(light_shape[i].x>2.5){
        vec3 f=light_direction[i].xyz,r=normalize(light_right[i].xyz-f*dot(f,light_right[i].xyz)),u=cross(r,f);
        vec3 local=vec3(dot(d,r),dot(d,u),dot(d,f));vec3 a=abs(local);
        if(a.z>=a.x&&a.z>=a.y)return 0;
        if(a.x>=a.y)return local.x>0.0?1:2;
        return local.y>0.0?3:4;
    }
    if(light_shape[i].x>1.5)return 0;
    vec3 a=abs(d);if(a.x>=a.y&&a.x>=a.z)return d.x>0.0?0:1;if(a.y>=a.z)return d.y>0.0?2:3;return d.z>0.0?4:5;
}
float local_shadow(int i,vec3 world,vec3 normal){
    int slot=int(light_shadow[i/4][i%4]);if(slot<0)return 1.0;
    vec3 to_light=normalize(light_position[i].xyz-world);
    float bias=max(.00005,light_control[i].y*(1.0-max(dot(normal,to_light),0.0))*.25);
    return face_visibility(slot+local_face(i,world),world,bias,light_control[i].z);
}
float light_attenuation(int i,vec3 world){
    vec3 delta=light_position[i].xyz-world;float d=length(delta),radius=max(light_position[i].w,.001);
    float denominator=light_shape[i].w>.5?max(d*d,max(.0025,light_control[i].x*light_control[i].x)):1.0+d*d;
    float result=pow(clamp(1.0-pow(d/radius,4.0),0.0,1.0),2.0)/denominator;
    if(light_shape[i].x>1.5&&light_shape[i].x<2.5)result*=smoothstep(light_direction[i].w,max(light_direction[i].w+.0001,light_shape[i].y),dot(-delta/max(d,.001),light_direction[i].xyz));
    if(light_shape[i].x>2.5)result*=max(dot(-delta/max(d,.001),light_direction[i].xyz),0.0);
    return result;
}
