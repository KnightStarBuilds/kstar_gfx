#ifndef KSTAR_GFX_OPENGL_H
#define KSTAR_GFX_OPENGL_H
#include "gfx.h"
/* The host owns a current GL 4.1 core context and keeps it current on this thread.
   These hooks call the platform GL loader/presenter, never select an RHI backend. */
typedef void *(*kgfx_gl_get_proc_fn)(const char *name);
typedef void (*kgfx_gl_present_fn)(void *user);
typedef struct kgfx_gl_config {
    kgfx_gl_get_proc_fn get_proc;
    kgfx_gl_present_fn present;
    void *user;
} kgfx_gl_config;
#endif
