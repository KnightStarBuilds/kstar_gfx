#ifndef KGFX_GNM_GUARDBAND_H
#define KGFX_GNM_GUARDBAND_H

#include <math.h>

/* PA_CL_GB_*_CLIP_ADJ is a distance in normalized clip coordinates.
 * With 16.8 raster quantization and zero hardware screen offset, keep
 * post-viewport coordinates inside the signed 16-bit pixel range. */
static inline float kgfx_gnm_guardband_axis(float scale, float offset) {
  const double magnitude = fabs((double)scale);
  if (magnitude == 0.0)
    return 1.0f;
  const double lower = (32768.0 + (double)offset) / magnitude;
  const double upper = (32767.0 - (double)offset) / magnitude;
  const double limit = lower < upper ? lower : upper;
  if (limit <= 1.0)
    return 1.0f;
  /* Round inward: a rounded-up clip boundary can overflow raster setup. */
  return fmaxf(1.0f, nextafterf((float)limit, 0.0f));
}

#endif
