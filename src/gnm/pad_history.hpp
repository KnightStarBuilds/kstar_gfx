#pragma once
#include <cstddef>
#include <cstdint>

namespace kgfx_gnm_demo {
// Keep button edges from every queued report, even when a press and release
// both arrive during GPU work. Analog controls use the latest complete report.
template <class Report> struct PadHistory {
  Report current{};
  uint32_t previous_buttons{};

  uint32_t consume(const Report *reports, size_t count) {
    uint32_t pressed{};
    for (size_t i = 0; i < count; ++i) {
      current = reports[i];
      if (!current.connected) {
        previous_buttons = 0;
        continue;
      }
      pressed |= current.buttons & ~previous_buttons;
      previous_buttons = current.buttons;
    }
    return pressed;
  }
};
} // namespace kgfx_gnm_demo
