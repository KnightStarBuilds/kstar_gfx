#include "../../src/gnm/guardband.h"
#include <stdio.h>

#define CHECK(x) do { if (!(x)) { \
  fprintf(stderr, "guardband check failed at %d: %s\n", __LINE__, #x); \
  return 1; } } while (0)

typedef struct { double x, y, z, w; } Vertex;

static double distance_to_plane(Vertex v, unsigned plane, double x, double y) {
  switch (plane) {
  case 0: return v.z; /* GNM's zero-to-W near clip plane. */
  case 1: return v.x + x * v.w;
  case 2: return x * v.w - v.x;
  case 3: return v.y + y * v.w;
  default: return y * v.w - v.y;
  }
}

static unsigned clip(Vertex *polygon, unsigned count, unsigned plane,
                     double x, double y) {
  Vertex result[16];
  unsigned n = 0;
  for (unsigned i = 0; i < count; ++i) {
    const Vertex a = polygon[i], b = polygon[(i + 1) % count];
    const double da = distance_to_plane(a, plane, x, y);
    const double db = distance_to_plane(b, plane, x, y);
    if (da >= 0)
      result[n++] = a;
    if ((da < 0) != (db < 0)) {
      const double t = da / (da - db);
      result[n++] = (Vertex){a.x + (b.x-a.x)*t, a.y + (b.y-a.y)*t,
                            a.z + (b.z-a.z)*t, a.w + (b.w-a.w)*t};
    }
  }
  for (unsigned i = 0; i < n; ++i)
    polygon[i] = result[i];
  return n;
}

static int check_axis(float scale, float offset) {
  const double band = kgfx_gnm_guardband_axis(scale, offset);
  CHECK(isfinite(band) && band >= 1);
  const double a = offset - fabs((double)scale) * band;
  const double b = offset + fabs((double)scale) * band;
  CHECK(a >= -32768.0 && b <= 32767.0);
  /* Normal viewport edges survive; off-screen vertices are clipped only
   * at raster setup's representable coordinate limits. */
  CHECK(band > 1 || fabs((double)scale) >= 16383);
  return 0;
}

static int check_floor(void) {
  const double scale_x = 960, scale_y = -540, offset_x = 960, offset_y = 540;
  const double band_x = kgfx_gnm_guardband_axis((float)scale_x, (float)offset_x);
  const double band_y = kgfx_gnm_guardband_axis((float)scale_y, (float)offset_y);
  unsigned oversized = 0, near_clipped = 0;
  /* The demo's 18x18 ground crosses the camera near plane while orbiting.
   * Project it independently, then clip before the fixed-point raster stage. */
  for (unsigned angle = 0; angle < 32; ++angle) {
    const double yaw = angle * (6.283185307179586 / 32), elevation = .22;
    const double sy = sin(yaw), cy = cos(yaw), se = sin(elevation), ce = cos(elevation);
    const double f = 1 / tan(48 * 3.141592653589793 / 360);
    const double corners[4][2] = {{-9,-9},{9,-9},{-9,9},{9,9}};
    const unsigned indices[2][3] = {{0,2,1},{1,2,3}};
    for (unsigned tri = 0; tri < 2; ++tri) {
      Vertex polygon[16];
      for (unsigned v = 0; v < 3; ++v) {
        const double x = corners[indices[tri][v]][0];
        const double z = corners[indices[tri][v]][1];
        const double y = -1.65; /* ground Y relative to camera target */
        const double w = 10.8 - (sy*ce*x + se*y + cy*ce*z);
        polygon[v] = (Vertex){(cy*x-sy*z)*f/(16.0/9.0),
                             (-sy*se*x+ce*y-cy*se*z)*f,
                             (100.0*w-5.0)/99.95, w};
        if (polygon[v].z < 0)
          ++near_clipped;
      }
      unsigned count = clip(polygon, 3, 0, band_x, band_y);
      for (unsigned v = 0; v < count; ++v) {
        const double x = scale_x*polygon[v].x/polygon[v].w+offset_x;
        const double y = scale_y*polygon[v].y/polygon[v].w+offset_y;
        if (x < -32768 || x > 32767 || y < -32768 || y > 32767)
          ++oversized;
      }
      for (unsigned plane = 1; plane < 5; ++plane)
        count = clip(polygon, count, plane, band_x, band_y);
      for (unsigned v = 0; v < count; ++v) {
        CHECK(polygon[v].w > 0);
        const double x = scale_x*polygon[v].x/polygon[v].w+offset_x;
        const double y = scale_y*polygon[v].y/polygon[v].w+offset_y;
        CHECK(x >= -32768 && x <= 32767 && y >= -32768 && y <= 32767);
      }
    }
  }
  CHECK(near_clipped > 0 && oversized > 0);
  printf("GNM floor clip regression: %u near-plane vertices, %u overflowing screen vertices constrained\n",
         near_clipped, oversized);
  return 0;
}

int main(void) {
  const float dimensions[] = {1,128,720,1080,1920,2048,4096,16384};
  const float origins[] = {-12000,-8192,-17,0,31,4096,12000};
  for (unsigned i = 0; i < sizeof(dimensions)/sizeof(dimensions[0]); ++i)
    for (unsigned j = 0; j < sizeof(origins)/sizeof(origins[0]); ++j) {
      const float half = dimensions[i] * .5f;
      CHECK(check_axis(half, origins[j] + half) == 0);
      CHECK(check_axis(-half, origins[j] + half) == 0);
    }
  CHECK(check_axis(16383.5f,16383.5f) == 0);
  CHECK(check_axis(16384,-16384) == 0);
  CHECK(kgfx_gnm_guardband_axis(0,0) == 1);
  CHECK(check_floor() == 0);
  puts("GNM guardband bounds PASS");
  return 0;
}
