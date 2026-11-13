// Shared std140 layouts are checked against the generated reflection headers.
layout(std140,binding=0) uniform kgfx_u0 {
    mat4 view_projection;
    mat4 inverse_view_projection;
    mat4 directional_projection;
    mat4 reflection_projection;
    mat4 spot_projection[32];
    mat4 directional_far;
    vec4 cascade;
    vec4 probe_origin;
    vec4 probe_spacing;
    vec4 probe_counts;
    vec4 sky_color;
    mat4 volume_inverse[4];
    vec4 volume_params[4];
    vec4 volume_color[4];
    vec4 volume_info;
    vec4 camera_time;
    vec4 sun_direction;
    vec4 sun_color;
    vec4 viewport;
    vec4 params;
    vec4 fog;
    vec4 clip_plane;
    vec4 camera_right;
    vec4 camera_up;
    vec4 post;
};
layout(std140,binding=1) uniform kgfx_u1 {
    mat4 model;
    mat4 normal_matrix;
    vec4 base_color;
    vec4 material;
    vec4 emissive;
    vec4 surface;
    vec4 tint;
    vec4 object_options;
    vec4 light_position[8];
    vec4 light_color[8];
    vec4 light_direction[8];
    vec4 light_shadow[2];
    vec4 light_right[8];
    vec4 light_shape[8];
    vec4 light_control[8];
    vec4 light_diffuse[2];
    vec4 baked_direct[6];
    vec4 baked_indirect[6];
    vec4 mask_uv;
    vec4 mask_params;
    mat4 decal_projection[8];
    vec4 decal_color[8];
    vec4 decal_params[8];
    vec4 lightmap_u[6];
    vec4 lightmap_v[6];
    vec4 lightmap_options;
};
const float PI=3.14159265359;
vec2 image_uv(vec2 p){
#ifdef KGFX_GNM
    p.y=1.0-p.y;
#endif
    return p;
}
#ifdef KGFX_FRAGMENT
vec2 screen_uv(){return gl_FragCoord.xy*viewport.zw;}
#endif
vec3 ray_at(vec2 uv){
    vec4 w=inverse_view_projection*vec4(image_uv(uv)*2.0-1.0,1.0,1.0);
    return normalize(w.xyz/w.w-camera_time.xyz);
}
vec3 fog_color(){return mix(vec3(0.032,0.060,0.09),vec3(0.1,0.14,0.18),sun_color.w);}
vec3 apply_fog(vec3 radiance,vec3 world){
    float d=length(camera_time.xyz-world);
    float density=fog.x*exp(-max(world.y,0.0)*fog.y);
    float extinction=exp(-d*density);
    return mix(fog_color(),radiance,extinction);
}
