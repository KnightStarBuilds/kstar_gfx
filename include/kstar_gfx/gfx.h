#ifndef KSTAR_GFX_H
#define KSTAR_GFX_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

#define KGFX_ABI_VERSION 1u
#define KGFX_MAX_TEXTURE_BINDINGS 16u
#define KGFX_MAX_UNIFORM_BINDINGS 4u
#define KGFX_MAX_VERTEX_ATTRIBUTES 8u

typedef struct kgfx_device kgfx_device;
typedef struct kgfx_buffer kgfx_buffer;
typedef struct kgfx_texture kgfx_texture;
typedef struct kgfx_sampler kgfx_sampler;
typedef struct kgfx_pipeline kgfx_pipeline;
typedef struct kgfx_readback kgfx_readback;
typedef uint64_t kgfx_completion;

typedef enum kgfx_result { KGFX_OK=0, KGFX_INVALID_ARGUMENT, KGFX_UNSUPPORTED,
    KGFX_OUT_OF_MEMORY, KGFX_DRIVER_ERROR, KGFX_NOT_READY, KGFX_IO_ERROR } kgfx_result;
typedef enum kgfx_format { KGFX_RGBA8, KGFX_SRGBA8, KGFX_RGBA16F, KGFX_RGBA32F,
    KGFX_R32F, KGFX_DEPTH32F } kgfx_format;
typedef enum kgfx_load { KGFX_LOAD, KGFX_CLEAR, KGFX_DISCARD } kgfx_load;
typedef enum kgfx_filter { KGFX_NEAREST, KGFX_LINEAR } kgfx_filter;
typedef enum kgfx_wrap { KGFX_CLAMP, KGFX_REPEAT } kgfx_wrap;
typedef enum kgfx_compare { KGFX_COMPARE_ALWAYS, KGFX_COMPARE_LESS,
    KGFX_COMPARE_LEQUAL } kgfx_compare;
typedef enum kgfx_cull { KGFX_CULL_NONE, KGFX_CULL_BACK, KGFX_CULL_FRONT } kgfx_cull;
typedef enum kgfx_blend { KGFX_BLEND_OPAQUE, KGFX_BLEND_ALPHA,
    KGFX_BLEND_PREMULTIPLIED, KGFX_BLEND_ADDITIVE } kgfx_blend;
typedef enum kgfx_access { KGFX_ACCESS_SAMPLED, KGFX_ACCESS_ATTACHMENT,
    KGFX_ACCESS_COPY_SOURCE, KGFX_ACCESS_COPY_DESTINATION } kgfx_access;
typedef enum kgfx_vertex_type { KGFX_FLOAT2, KGFX_FLOAT3, KGFX_FLOAT4,
    KGFX_UNORM8X4 } kgfx_vertex_type;
enum { KGFX_BUFFER_VERTEX=1u, KGFX_BUFFER_INDEX=2u, KGFX_BUFFER_UNIFORM=4u,
    KGFX_BUFFER_DYNAMIC=8u, KGFX_BUFFER_TRANSFER=16u };
enum { KGFX_TEXTURE_SAMPLED=1u, KGFX_TEXTURE_TARGET=2u, KGFX_TEXTURE_CUBE=4u };

typedef void (*kgfx_log_fn)(void *user, const char *message);
typedef struct kgfx_device_desc {
    uint32_t abi_version, width, height, frames_in_flight;
    const void *backend_config;
    kgfx_log_fn log;
    void *log_user;
} kgfx_device_desc;
typedef struct kgfx_caps {
    uint32_t max_texture_size, max_samples, uniform_alignment;
    uint32_t max_texture_bindings;
    char renderer[256];
} kgfx_caps;
typedef struct kgfx_buffer_desc {
    size_t size;
    uint32_t usage;
    const void *initial_data;
} kgfx_buffer_desc;
typedef struct kgfx_texture_desc {
    uint32_t width, height, mip_levels, samples, usage;
    kgfx_format format;
} kgfx_texture_desc;
typedef struct kgfx_sampler_desc {
    kgfx_filter min_filter, mag_filter;
    kgfx_wrap wrap_u, wrap_v;
    uint32_t mipmapped, compare_enabled;
    kgfx_compare compare;
} kgfx_sampler_desc;
typedef struct kgfx_vertex_attribute {
    uint32_t location, offset, buffer_slot, divisor;
    kgfx_vertex_type type;
} kgfx_vertex_attribute;
/* Artifacts are generated GLSL for GL and native shader packages for GNM.
   Entry points are main; binding layouts are fixed by generated metadata. */
typedef struct kgfx_shader_artifact {
    const void *data;
    size_t size;
} kgfx_shader_artifact;
typedef struct kgfx_pipeline_desc {
    kgfx_shader_artifact vertex, fragment;
    kgfx_vertex_attribute attributes[KGFX_MAX_VERTEX_ATTRIBUTES];
    uint32_t attribute_count, vertex_stride[2];
    kgfx_format color_format, depth_format;
    uint32_t samples, depth_test, depth_write;
    kgfx_compare depth_compare;
    kgfx_cull cull;
    kgfx_blend blend;
    uint32_t wireframe;
} kgfx_pipeline_desc;
typedef struct kgfx_pass_desc {
    /* Attachments use mip zero; cubemap attachments are unsupported. */
    kgfx_texture *color, *depth;
    /* Zero selects the attachment size, or device size for presentation. */
    uint32_t width, height;
    kgfx_load color_load, depth_load;
    float clear_color[4], clear_depth;
} kgfx_pass_desc;
typedef struct kgfx_bindings {
    kgfx_buffer *vertex[2], *index;
    size_t vertex_offset[2], index_offset;
    uint32_t index_size;
    kgfx_buffer *uniforms[KGFX_MAX_UNIFORM_BINDINGS];
    size_t uniform_offset[KGFX_MAX_UNIFORM_BINDINGS];
    size_t uniform_size[KGFX_MAX_UNIFORM_BINDINGS]; /* 0: bytes after offset. */
    kgfx_texture *textures[KGFX_MAX_TEXTURE_BINDINGS];
    kgfx_sampler *samplers[KGFX_MAX_TEXTURE_BINDINGS];
} kgfx_bindings;
typedef struct kgfx_stats {
    uint64_t draws, instances, pipeline_binds, upload_bytes, readback_bytes;
    uint64_t live_buffers, live_textures, live_pipelines, live_samplers;
    uint64_t frame_serial, completed_serial, steady_state_allocations;
    double cpu_submit_ms;
} kgfx_stats;
typedef struct kgfx_readback_data {
    const void *pixels;
    size_t size, row_pitch;
    uint32_t width, height;
    kgfx_format format;
} kgfx_readback_data;

kgfx_result kgfx_device_create(const kgfx_device_desc *, kgfx_device **);
void kgfx_device_destroy(kgfx_device *);
const kgfx_caps *kgfx_device_caps(const kgfx_device *);
const char *kgfx_device_error(const kgfx_device *);
kgfx_result kgfx_device_resize(kgfx_device *, uint32_t, uint32_t);
void kgfx_device_wait_idle(kgfx_device *);
kgfx_result kgfx_buffer_create(kgfx_device *, const kgfx_buffer_desc *, kgfx_buffer **);
kgfx_result kgfx_buffer_update(kgfx_device *, kgfx_buffer *, size_t, const void *, size_t);
void kgfx_buffer_destroy(kgfx_device *, kgfx_buffer *);
kgfx_result kgfx_texture_create(kgfx_device *, const kgfx_texture_desc *, kgfx_texture **);
/* All CPU image rows are top-to-bottom. face is 0 for 2D, 0..5 for cubemaps. */
kgfx_result kgfx_texture_upload(kgfx_device *, kgfx_texture *, uint32_t mip,
    uint32_t face, const void *pixels, size_t row_pitch);
void kgfx_texture_destroy(kgfx_device *, kgfx_texture *);
kgfx_result kgfx_sampler_create(kgfx_device *, const kgfx_sampler_desc *, kgfx_sampler **);
void kgfx_sampler_destroy(kgfx_device *, kgfx_sampler *);
kgfx_result kgfx_pipeline_create(kgfx_device *, const kgfx_pipeline_desc *, kgfx_pipeline **);
void kgfx_pipeline_destroy(kgfx_device *, kgfx_pipeline *);
kgfx_result kgfx_frame_begin(kgfx_device *);
kgfx_result kgfx_pass_begin(kgfx_device *, const kgfx_pass_desc *);
void kgfx_pass_end(kgfx_device *);
void kgfx_bind_pipeline(kgfx_device *, kgfx_pipeline *);
void kgfx_bind_resources(kgfx_device *, const kgfx_bindings *);
void kgfx_viewport(kgfx_device *, int32_t x, int32_t y, uint32_t w, uint32_t h);
void kgfx_scissor(kgfx_device *, int32_t x, int32_t y, uint32_t w, uint32_t h);
void kgfx_draw(kgfx_device *, uint32_t first, uint32_t count, uint32_t instances);
void kgfx_draw_indexed(kgfx_device *, uint32_t first, uint32_t count, uint32_t instances);
kgfx_result kgfx_texture_transition(kgfx_device *, kgfx_texture *, kgfx_access);
kgfx_result kgfx_texture_copy(kgfx_device *, kgfx_texture *src, kgfx_texture *dst,
    uint32_t src_x, uint32_t src_y, uint32_t dst_x, uint32_t dst_y, uint32_t w, uint32_t h);
kgfx_result kgfx_texture_resolve(kgfx_device *, kgfx_texture *src, kgfx_texture *dst);
kgfx_completion kgfx_submit(kgfx_device *, uint32_t present);
uint32_t kgfx_is_complete(kgfx_device *, kgfx_completion);
kgfx_result kgfx_wait(kgfx_device *, kgfx_completion, uint64_t timeout_ns);
kgfx_result kgfx_readback_begin(kgfx_device *, kgfx_texture *, kgfx_readback **);
kgfx_result kgfx_readback_map(kgfx_device *, kgfx_readback *, kgfx_readback_data *);
void kgfx_readback_destroy(kgfx_device *, kgfx_readback *);
void kgfx_get_stats(const kgfx_device *, kgfx_stats *);
const char *kgfx_backend_name(void);

#ifdef __cplusplus
}
#endif
#endif
