#version 450
#extension GL_GOOGLE_include_directive : require
#define KGFX_FRAGMENT
#define KGFX_VOLUME_RAYMARCH
#include "showcase_common.glsl"
layout(location=0) out vec4 out_color;
layout(binding=11) uniform sampler2D kgfx_t3;
layout(binding=12) uniform sampler2D kgfx_t4;
layout(binding=13) uniform sampler2D kgfx_t5;
layout(binding=14) uniform sampler2D kgfx_t6;
layout(binding=15) uniform sampler2D kgfx_t7;
layout(binding=16) uniform sampler2D kgfx_t8;
layout(binding=17) uniform sampler2D kgfx_t9;
layout(binding=18) uniform sampler2D kgfx_t10;
layout(binding=20) uniform sampler2D kgfx_t12;
layout(binding=21) uniform sampler2D kgfx_t13;
#include "showcase_volume_data.glsl"
#define KGFX_LOCAL_SHADOW_TEXTURE kgfx_t9
#include "showcase_visibility.glsl"
void main(){
    vec2 q=screen_uv();vec3 ray=ray_at(q);float opaque_distance=texture(kgfx_t10,q).r;
    float near_interface=texture(kgfx_t12,q).r,far_interface=texture(kgfx_t13,q).r;
    int band=int(params.w);float segment_start=band==0?0.0:band==1?near_interface:far_interface,segment_end=min(opaque_distance,band==0?near_interface:band==1?far_interface:opaque_distance);
    float start=1e6,end=0;
    for(int i=0;i<4;++i){if(float(i)>=volume_info.x)break;vec2 interval=volume_interval(i,camera_time.xyz,ray);if(interval.y>max(interval.x,0.0)){start=min(start,max(interval.x,0.0));end=max(end,min(interval.y,opaque_distance));}}
    if(end<=start||segment_end<=segment_start){out_color=vec4(0);return;}
    float step_size=(end-start)/volume_info.y;int first=max(0,int(ceil((segment_start-start)/step_size-.5)));
    vec3 radiance=vec3(0);float transmission=1.0;
    // The three glass/water bands partition one 48/24-sample ray lattice.
    // Overlapping volumes add extinction before the Beer-Lambert integration.
    for(int index=0;index<48;++index){int sample_index=first+index;if(float(sample_index)>=volume_info.y)break;float distance_along=start+(float(sample_index)+.5)*step_size;if(distance_along>=segment_end)break;
        vec3 p=camera_time.xyz+ray*distance_along;float density=0.0;vec3 emission=vec3(0),scatter=vec3(0);
        for(int i=0;i<4;++i){if(float(i)>=volume_info.x)break;vec3 field=volume_lod_sample(i,p,ray);float d=max(field.r,field.g*.15)*volume_params[i].y;if(d<=0.0)continue;density+=d;
            vec3 fire_color=mix(vec3(1.0,.10,.005),vec3(1.0,.85,.32),clamp(max(field.b,sqrt(field.g)),0.0,1.0));emission+=fire_color*field.g*volume_params[i].z;
            // Short same-volume light path supplies self attenuation.
            float self_visibility=exp(-volume_sample(i,p+normalize(sun_direction.xyz)*.22).r*volume_params[i].y*.44);
            scatter+=d*volume_color[i].rgb*vec3(.34,.39,.43)*self_visibility;
        }
        if(density<=0.0)continue;
        vec3 illumination=vec3(.1)+sun_color.rgb*sun_direction.w*.14;
        for(int i=0;i<8;++i){if(light_color[i].w<=0.0||light_control[i].w<=0.0)continue;float a=light_attenuation(i,p);if(a>.0001)illumination+=light_color[i].rgb*light_color[i].w*a*.12*light_control[i].w*local_shadow(i,p,-ray);}
        float opacity=1.0-exp(-density*step_size);vec3 source=(scatter*illumination+emission)/max(density,.001);
        radiance+=transmission*source*opacity;transmission*=1.0-opacity;if(transmission<.01)break;
    }
    out_color=vec4(radiance,1.0-transmission);
}
