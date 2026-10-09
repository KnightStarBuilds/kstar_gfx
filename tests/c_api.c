#include <kstar_gfx/gfx.h>
#include <kstar_gfx/gnm.h>
#include <kstar_gfx/opengl.h>
#include <string.h>
_Static_assert(sizeof(kgfx_completion) == 8, "Completion ABI must be 64 bits");
int main(void) {
  kgfx_device *device = 0;
  kgfx_device_desc desc = {0};
  if (kgfx_device_create(&desc, &device) != KGFX_INVALID_ARGUMENT || device)
    return 1;
  return strlen(kgfx_backend_name()) == 0;
}
