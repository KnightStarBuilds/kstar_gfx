#include <kstar_gfx/gfx.h>
#include <kstar_gfx/gnm.h>
#include <kstar_gfx/opengl.h>
#include <string.h>
_Static_assert(sizeof(kgfx_completion) == 8, "Completion ABI must be 64 bits");
_Static_assert(sizeof(kgfx_gpu_timer_result) == 16, "Timer result ABI must be 128 bits");
int main(void) {
  kgfx_device *device = 0;
  kgfx_device_desc desc = {0};
  if (kgfx_device_create(&desc, &device) != KGFX_INVALID_ARGUMENT || device)
    return 1;
  kgfx_gpu_timer *timer = (kgfx_gpu_timer *)(uintptr_t)1;
  kgfx_gpu_timer_result timing = {17, 29};
  if (kgfx_gpu_timer_create(0, &timer) != KGFX_INVALID_ARGUMENT || timer ||
      kgfx_gpu_timer_create(0, 0) != KGFX_INVALID_ARGUMENT ||
      kgfx_gpu_timer_begin(0, 0) != KGFX_INVALID_ARGUMENT ||
      kgfx_gpu_timer_end(0, 0) != KGFX_INVALID_ARGUMENT ||
      kgfx_gpu_timer_read(0, 0, &timing) != KGFX_INVALID_ARGUMENT ||
      timing.frame_serial != 17 || timing.elapsed_ns != 29)
    return 2;
  kgfx_gpu_timer_destroy(0, 0);
  return strlen(kgfx_backend_name()) == 0;
}
