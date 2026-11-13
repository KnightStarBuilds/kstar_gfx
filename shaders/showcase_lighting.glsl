#include "showcase_visibility.glsl"
vec2 lookup_uv(vec2 p,int table){
    vec2 pixel=table==0?vec2(.5)+clamp(p,vec2(0),vec2(1))*255.0:vec2(.5,table==1?256.5:320.5)+clamp(p,vec2(0),vec2(1))*63.0;
    vec2 uv=pixel/vec2(256,384);
#ifndef KGFX_GNM
    uv.y=1.0-uv.y;
#endif
    return uv;
}
float sun_visibility(mat4 projection,vec3 world,vec3 n,vec3 l,int tile){
    vec4 c=projection*vec4(world,1);vec3 p=c.xyz/c.w*.5+.5;
    if(any(lessThan(p,vec3(0)))||any(greaterThan(p,vec3(1))))return 1.0;
    vec2 texel=1.0/vec2(textureSize(kgfx_t7,0));vec2 origin=vec2(float(tile)*.5,0);
#ifndef KGFX_GNM
    origin.y=.5;
#endif
    vec2 q=image_uv(p.xy)*.5+origin;
    float bias=max(.001*(1.0-dot(n,l)),.0002),sum=0;
    for(int y=0;y<2;++y)for(int x=0;x<2;++x){vec2 tap=clamp(q+(vec2(x,y)-.5)*1.5*texel,origin+texel*.5,origin+vec2(.5)-texel*.5),moving_tap=tap;moving_tap.y+=origin.y>.25?-.5:.5;float depth=min(texture(kgfx_t7,tap).r,texture(kgfx_t7,moving_tap).r);sum+=p.z-bias<=depth?1.0:0.0;}
    return sum*.25;
}
float directional_shadow(vec3 world,vec3 n,vec3 l){if(cascade.w<.5)return 1.0;float distance_to_eye=length(world-camera_time.xyz);float blend=smoothstep(cascade.x,cascade.y,distance_to_eye);if(blend<=0.0)return sun_visibility(directional_projection,world,n,l,0);float far_value=sun_visibility(directional_far,world,n,l,1);if(blend>=1.0)return far_value;return mix(sun_visibility(directional_projection,world,n,l,0),far_value,blend);}
// LTC polygon integral, following Heitz et al., SIGGRAPH 2016. The pinned
// inverse-transform/amplitude tables and their license ship in the package.
float cosine_edge(vec3 a,vec3 b){float x=clamp(dot(a,b),-.99999,.99999),y=abs(x);float fit=(.8543985+(.4965155+.0145206*y)*y)/(3.4175940+(4.1616724+y)*y);float edge=x>0.0?fit:.5*inversesqrt(max(1.0-x*x,1e-7))-fit;return cross(a,b).z*edge;}
float cosine_polygon(vec3 points[4],mat3 transform){
    vec3 poly[4];for(int i=0;i<4;++i)poly[i]=transform*points[i];
    if(poly[0].z>0.0&&poly[1].z>0.0&&poly[2].z>0.0&&poly[3].z>0.0){vec3 a=normalize(poly[0]),b=normalize(poly[1]),c=normalize(poly[2]),d=normalize(poly[3]);return abs(cosine_edge(a,b)+cosine_edge(b,c)+cosine_edge(c,d)+cosine_edge(d,a));}
    vec3 clipped[8];int count=0;
    for(int i=0;i<4;++i){vec3 a=poly[i],b=poly[(i+1)%4];bool ai=a.z>0.0,bi=b.z>0.0;if(ai)clipped[count++]=a;if(ai!=bi)clipped[count++]=mix(a,b,a.z/(a.z-b.z));}
    if(count<3)return 0.0;
    // The reference LTC edge fit avoids a transcendental acos per edge.
    // Its coefficient already includes the 1/(2*pi) normalization.
    float integral=0;for(int i=0;i<8;++i){if(i>=count)break;integral+=cosine_edge(normalize(clipped[i]),normalize(clipped[(i+1)%count]));}
    return abs(integral);
}
vec3 rect_light(int i,vec3 world,vec3 n,vec3 v,vec3 albedo,float metal,float rough,float diffuse_weight){
    vec3 center=light_position[i].xyz,forward=light_direction[i].xyz;if(dot(world-center,forward)<=0.0)return vec3(0);
    vec3 r=normalize(light_right[i].xyz-forward*dot(forward,light_right[i].xyz)),u=cross(r,forward);
    vec3 x=r*light_right[i].w*.5,y=u*light_shape[i].z*.5,p=center-world;
    vec3 points[4];points[0]=p-x-y;points[1]=p+x-y;points[2]=p+x+y;points[3]=p-x+y;
    vec3 t=v-n*dot(n,v);if(dot(t,t)<1e-6)t=abs(n.y)<.9?cross(n,vec3(0,1,0)):cross(n,vec3(1,0,0));t=normalize(t);
    mat3 basis=transpose(mat3(t,cross(n,t),n));
    vec2 coord=vec2(rough,sqrt(1.0-clamp(dot(n,v),0.0,1.0)));vec4 fit=texture(kgfx_t6,lookup_uv(coord,1)),amp=texture(kgfx_t6,lookup_uv(coord,2));
    mat3 inverse_ltc=mat3(vec3(fit.x,0,fit.y),vec3(0,1,0),vec3(fit.z,0,fit.w));
    float diffuse=cosine_polygon(points,basis),specular=cosine_polygon(points,inverse_ltc*basis);
    vec3 f0=mix(vec3(.04),albedo,metal);float area=max(light_right[i].w*light_shape[i].z,.001);
    float cutoff=pow(clamp(1.0-pow(length(p)/max(light_position[i].w,.01),4.0),0.0,1.0),2.0);
    return light_color[i].rgb*(light_color[i].w/area)*cutoff*((1.0-f0)*(1.0-metal)*albedo*diffuse*diffuse_weight+specular*(f0*amp.x+(1.0-f0)*amp.y));
}
float bilateral_ao(vec2 q,float depth){
    vec2 size=vec2(textureSize(kgfx_t9,0)),pixel=q*size-.5,base_pixel=floor(pixel),f=fract(pixel);float total=0,weight=0;
    for(int y=0;y<2;++y)for(int x=0;x<2;++x){vec2 sample_uv=(base_pixel+vec2(x,y)+.5)/size;float w=(x==0?1.0-f.x:f.x)*(y==0?1.0-f.y:f.y);w*=exp(-abs(texture(kgfx_t10,sample_uv).r-depth)*4.0);total+=texture(kgfx_t9,sample_uv).r*w;weight+=w;}
    return weight>.0001?total/weight:1.0;
}
#ifdef KGFX_OPAQUE
vec4 surface_mask(sampler2D atlas,vec2 uv,int mask){int tile=max(mask-1,0);vec2 local=clamp(fract(uv),vec2(.004),vec2(.996));vec2 q=(vec2(float(tile%2),float(tile/2))+local)*.5;
#ifndef KGFX_GNM
q.y=1.0-q.y;
#endif
return texture(atlas,q);}
vec4 probe_texel(sampler2D atlas,int row,int column){
#ifndef KGFX_GNM
row=191-row;
#endif
return texelFetch(atlas,ivec2(column,row),0);}
void sample_probes(sampler2D atlas,vec3 world,vec3 n,out vec3 baked,out vec3 bounce,out float sky_visibility,out float coverage){
    baked=vec3(0);bounce=vec3(0);sky_visibility=1.0;coverage=0;if(probe_origin.w<.5)return;sky_visibility=0.0;
    vec3 cell=clamp((world-probe_origin.xyz)/max(probe_spacing.xyz,vec3(.01)),vec3(0),max(probe_counts.xyz-vec3(1.001),vec3(0))),fraction=fract(cell);ivec3 base_index=ivec3(floor(cell));float sum=0;
    // Four vertices of the containing tetrahedron provide continuous local
    // interpolation while halving the visibility/irradiance texture fetches.
    ivec3 order=ivec3(0,1,2);if(fraction[order.x]<fraction[order.y])order.xy=order.yx;if(fraction[order.y]<fraction[order.z])order.yz=order.zy;if(fraction[order.x]<fraction[order.y])order.xy=order.yx;
    vec4 weights=vec4(1.0-fraction[order.x],fraction[order.x]-fraction[order.y],fraction[order.y]-fraction[order.z],fraction[order.z]);ivec3 corner=ivec3(0);
    for(int vertex=0;vertex<4;++vertex){if(vertex>0)corner[order[vertex-1]]=1;ivec3 index=min(base_index+corner,ivec3(probe_counts.xyz)-ivec3(1));int row=index.x+int(probe_counts.x)*(index.y+int(probe_counts.y)*index.z);
        vec3 delta=world-(probe_origin.xyz+vec3(index)*probe_spacing.xyz);float distance_to_probe=length(delta);vec3 direction=delta/max(distance_to_probe,.001);float mean=0,second=0,sky=0;vec3 b=vec3(0),d=vec3(0);
        for(int axis=0;axis<3;++axis){int face=axis*2+(n[axis]<0.0?1:0),visibility_face=axis*2+(direction[axis]<0.0?1:0);float w=n[axis]*n[axis];b+=probe_texel(atlas,row,face).rgb*w;d+=probe_texel(atlas,row,6+face).rgb*w;sky+=probe_texel(atlas,row,12+face).b*w;vec2 moments=probe_texel(atlas,row,12+visibility_face).rg;mean+=moments.x*direction[axis]*direction[axis];second+=moments.y*direction[axis]*direction[axis];}
        float variance=max(second-mean*mean,.02),diff=max(distance_to_probe-mean,0.0),visibility=variance/(variance+diff*diff);visibility=visibility*visibility*visibility;
        float weight=weights[vertex]*visibility;
        // A normal-biased weight rejects probes behind the receiving surface.
        weight*=max(.05,dot(-direction,n));baked+=b*weight;bounce+=d*weight;sky_visibility+=sky*weight;sum+=weight;
    }
    if(sum>.0001){baked/=sum;bounce/=sum;sky_visibility/=sum;}coverage=clamp(sum*2.0,0.0,1.0);
}
#endif
