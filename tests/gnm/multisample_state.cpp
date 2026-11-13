#include "../../src/gnm/multisample_state.hpp"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <gnmdriver.h>

#define CHECK(condition)                                                       \
  do {                                                                         \
    if (!(condition)) {                                                        \
      std::fprintf(stderr, "MSAA packet check failed at %d: %s\n", __LINE__,    \
                   #condition);                                                \
      std::exit(1);                                                            \
    }                                                                          \
  } while (false)

struct Context {
  std::array<uint32_t, 1024> values{};
  std::array<bool, 1024> written{};

  void execute(const uint32_t *begin, const uint32_t *end) {
    for (const uint32_t *packet = begin; packet < end;) {
      CHECK((*packet >> 30) == 3);
      const size_t words = ((*packet >> 16) & 0x3fff) + 2;
      CHECK(words <= size_t(end - packet));
      if (((*packet >> 8) & 0xff) == 0x69) {
        const uint32_t first = packet[1];
        CHECK(words >= 3 && first + words - 2 <= values.size());
        for (size_t i = 2; i < words; ++i) {
          values[first + i - 2] = packet[i];
          written[first + i - 2] = true;
        }
      }
      packet += words;
    }
  }

  uint32_t get(uint32_t address) const {
    const uint32_t index = (address - 0x28000) / 4;
    CHECK(index < values.size() && written[index]);
    return values[index];
  }
};

static int signed_nibble(uint32_t value) {
  return (value & 8) ? int(value & 15) - 16 : int(value & 15);
}

static void check_state(const Context &context, unsigned samples) {
  const unsigned log_samples = samples == 4 ? 2 : 0;
  CHECK(G_028A48_MSAA_ENABLE(context.get(R_028A48_PA_SC_MODE_CNTL_0)) ==
        (samples > 1));
  const auto aa = context.get(R_028BE0_PA_SC_AA_CONFIG);
  CHECK(G_028BE0_MSAA_NUM_SAMPLES(aa) == log_samples);
  CHECK(G_028BE0_MSAA_EXPOSED_SAMPLES(aa) == log_samples);
  CHECK(G_028BE0_MAX_SAMPLE_DIST(aa) == (samples == 4 ? 6 : 0));
  const auto eqaa = context.get(R_028804_DB_EQAA);
  CHECK(G_028804_MAX_ANCHOR_SAMPLES(eqaa) == log_samples);
  CHECK(G_028804_MASK_EXPORT_NUM_SAMPLES(eqaa) == log_samples);
  CHECK(G_028804_ALPHA_TO_MASK_NUM_SAMPLES(eqaa) == log_samples);
  // Ordinary MSAA runs the pixel shader once and retains per-sample coverage.
  CHECK(G_028804_PS_ITER_SAMPLES(eqaa) == 0);
  CHECK(G_028804_HIGH_QUALITY_INTERSECTIONS(eqaa) == 1);
  CHECK(G_028804_STATIC_ANCHOR_ASSOCIATIONS(eqaa) == 1);
  for (unsigned high = 0; high < 2; ++high) {
    const auto priority = context.get(R_028BD4_PA_SC_CENTROID_PRIORITY_0 + 4 * high);
    for (unsigned slot = 0; slot < 8; ++slot)
      CHECK(((priority >> (slot * 4)) & 15) == (samples == 4 ? slot % 4 : 0));
  }
  constexpr int positions[4][2]{{-2, -6}, {2, 6}, {-6, 2}, {6, -2}};
  for (unsigned pixel = 0; pixel < 4; ++pixel)
    for (unsigned word = 0; word < 4; ++word) {
      const auto packed = context.get(
          R_028BF8_PA_SC_AA_SAMPLE_LOCS_PIXEL_X0Y0_0 + pixel * 16 + word * 4);
      for (unsigned sample = 0; sample < 4; ++sample)
        for (unsigned axis = 0; axis < 2; ++axis)
          CHECK(signed_nibble(packed >> (sample * 8 + axis * 4)) ==
                (samples == 4 && word == 0 ? positions[sample][axis] : 0));
    }
  CHECK(context.get(R_028C38_PA_SC_AA_MASK_X0Y0_X1Y0) == 0xffffffff);
  CHECK(context.get(R_028C3C_PA_SC_AA_MASK_X0Y1_X1Y1) == 0xffffffff);
}

int main() {
  Context context;
  std::array<uint32_t, 256> init{};
  const auto count = sceGnmDrawInitDefaultHardwareState350(init.data(), init.size());
  CHECK(count > 0 && count <= init.size());
  context.execute(init.data(), init.data() + count);
  // Reproduce the actual cause: attachment counts cannot override this reset.
  CHECK(G_028A48_MSAA_ENABLE(context.get(R_028A48_PA_SC_MODE_CNTL_0)) == 0);
  for (const unsigned samples : {1u, 4u, 1u, 4u}) {
    std::array<uint32_t, 128> packets{};
    auto *end = kgfx_gnm::write_multisample_state(packets.data(), samples);
    CHECK(end <= packets.data() + packets.size());
    // Every pass must establish all state independently of previous passes.
    context.written.fill(false);
    context.execute(packets.data(), end);
    check_state(context, samples);
  }
  std::puts("GNM MSAA packets: firmware reset and 1x/4x transitions passed");
}
