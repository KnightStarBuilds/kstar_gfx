#ifndef KSTAR_GFX_GNM_MULTISAMPLE_STATE_HPP
#define KSTAR_GFX_GNM_MULTISAMPLE_STATE_HPP

#include <cstdint>
#include <pm4/amdgfxregs.h>

namespace kgfx_gnm {

// Emit the complete pass-owned raster sample state. The firmware initializer
// disables MSAA independently of the color/depth attachment sample counts.
// Keep this packet builder shared with the host test: shadPS4 currently derives
// raster samples from attachments and cannot detect a missing MSAA_ENABLE bit.
inline uint32_t *write_multisample_state(uint32_t *out, uint32_t samples) {
  const uint32_t n = samples == 4 ? 2 : 0;
  auto reg = [&out](uint32_t address, uint32_t value) {
    *out++ = 0xc0016900; // SET_CONTEXT_REG, one register.
    *out++ = (address - 0x28000) / 4;
    *out++ = value;
  };
  reg(R_028A48_PA_SC_MODE_CNTL_0, S_028A48_MSAA_ENABLE(samples > 1));
  reg(R_028BE0_PA_SC_AA_CONFIG,
      S_028BE0_MSAA_NUM_SAMPLES(n) | S_028BE0_MSAA_EXPOSED_SAMPLES(n) |
          S_028BE0_MAX_SAMPLE_DIST(n ? 6 : 0));
  reg(R_028804_DB_EQAA,
      S_028804_MAX_ANCHOR_SAMPLES(n) | S_028804_MASK_EXPORT_NUM_SAMPLES(n) |
          S_028804_ALPHA_TO_MASK_NUM_SAMPLES(n) |
          S_028804_HIGH_QUALITY_INTERSECTIONS(1) |
          S_028804_STATIC_ANCHOR_ASSOCIATIONS(1));
  reg(R_028BD4_PA_SC_CENTROID_PRIORITY_0, n ? 0x32103210 : 0);
  reg(R_028BD8_PA_SC_CENTROID_PRIORITY_1, n ? 0x32103210 : 0);
  // Standard rotated grid in sixteenths of a pixel, ordered for EQAA:
  // (-2,-6), (2,6), (-6,2), (6,-2). All have equal centroid distance.
  // Set all four quad pixels and reset unused positions when returning to 1x.
  *out++ = 0xc0106900; // SET_CONTEXT_REG, sixteen consecutive registers.
  *out++ = (R_028BF8_PA_SC_AA_SAMPLE_LOCS_PIXEL_X0Y0_0 - 0x28000) / 4;
  for (uint32_t pixel = 0; pixel < 4; ++pixel)
    for (uint32_t word = 0; word < 4; ++word)
      *out++ = n && word == 0 ? 0xe62a62ae : 0;
  reg(R_028C38_PA_SC_AA_MASK_X0Y0_X1Y0, 0xffffffff);
  reg(R_028C3C_PA_SC_AA_MASK_X0Y1_X1Y1, 0xffffffff);
  return out;
}

} // namespace kgfx_gnm
#endif
