#include "../src/gnm/pad_history.hpp"
#include <cstdio>

struct Report {
  uint32_t buttons;
  bool connected;
  uint8_t stick;
};

static bool check(bool condition, const char *intent) {
  if (!condition)
    std::fprintf(stderr, "Pad history: %s\n", intent);
  return condition;
}

int main() {
  constexpr uint32_t options = 1u << 3, triangle = 1u << 12;
  kgfx_gnm_demo::PadHistory<Report> history;
  bool ok = true;
  // A short Escape/Options tap can finish while a slow GPU frame is pending.
  const Report tap[] = {{options, true, 80}, {0, true, 145}};
  ok &= check(history.consume(tap, 2) == options,
              "press followed by release between frames must still quit");
  ok &= check(history.current.buttons == 0 && history.current.stick == 145,
              "analog input must come from the newest report");
  ok &= check(history.consume(nullptr, 0) == 0 && history.current.stick == 145,
              "empty polls must preserve analog input without repeating edges");
  const Report held[] = {{triangle, true, 180}, {triangle, true, 190}};
  ok &= check(history.consume(held, 2) == triangle &&
                  history.consume(held + 1, 1) == 0,
              "held scene controls must not retrigger each frame");
  const Report reconnect[] = {{0, false, 0}, {triangle, true, 128}};
  ok &= check(history.consume(reconnect, 2) == triangle &&
                  history.current.connected && history.current.stick == 128,
              "reconnecting must reset old button state and use new analog input");
  const Report quit_disconnect[] = {{options, true, 128}, {0, false, 0}};
  ok &= check(history.consume(quit_disconnect, 2) == options,
              "a disconnect must not erase a queued quit request");
  return ok ? 0 : 1;
}
