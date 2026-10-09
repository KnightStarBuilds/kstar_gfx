#ifndef KSTAR_GFX_GNM_H
#define KSTAR_GFX_GNM_H
#include <stdint.h>
/* OpenOrbis owns the process runtime. Set present=0 for offscreen probes.
   Fixed arenas are per frame; exhaustion reports an error, never reallocates. */
typedef struct kgfx_gnm_config {
    uint32_t present;
    uint32_t command_bytes;
    uint32_t upload_bytes;
} kgfx_gnm_config;
#endif
