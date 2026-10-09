#include "../demos/demo.hpp"
#include "pad_history.hpp"
#include <cstdio>
#include <exception>
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <kstar_gfx/gnm.h>
#include <orbis/Pad.h>
#include <orbis/Sysmodule.h>
#include <orbis/UserService.h>
#include <orbis/libkernel.h>
namespace {
struct Controller {
  int32_t handle = -1;
  kgfx_gnm_demo::PadHistory<OrbisPadData> history;
  uint64_t previous_time = sceKernelGetProcessTime();
  ~Controller() { if (handle >= 0) scePadClose(handle); }
  void open() {
    int32_t user = -1;
    OrbisUserServiceInitializeParams params{ORBIS_KERNEL_PRIO_FIFO_NORMAL};
    if (sceSysmoduleLoadModuleInternal(ORBIS_SYSMODULE_INTERNAL_USER_SERVICE) < 0 ||
        sceSysmoduleLoadModuleInternal(ORBIS_SYSMODULE_INTERNAL_PAD) < 0)
      throw std::runtime_error("Cannot load PS4 user/controller services");
    const auto status = sceUserServiceInitialize(&params);
    if (status < 0 && uint32_t(status) != ORBIS_USER_SERVICE_ERROR_ALREADY_INITIALIZED)
      throw std::runtime_error("Cannot initialize PS4 user service");
    if (sceUserServiceGetInitialUser(&user) < 0 || scePadInit() < 0)
      throw std::runtime_error("Cannot initialize PS4 controller");
    handle = scePadOpen(user, ORBIS_PAD_PORT_TYPE_STANDARD, 0, nullptr);
    if (handle < 0) throw std::runtime_error("Cannot open PS4 controller");
  }
};
float stick_axis(uint8_t value) {
  const float normalized = std::clamp((float(value)-128.0f)/127.0f,-1.0f,1.0f);
  constexpr float deadzone = .15f;
  const float magnitude = std::abs(normalized);
  return magnitude < deadzone ? 0.0f :
      std::copysign((magnitude-deadzone)/(1.0f-deadzone), normalized);
}
bool controller_pump(void *user, demo::Input &input) {
  auto &controller = *static_cast<Controller *>(user);
  const auto now = sceKernelGetProcessTime();
  const float dt = std::clamp(float(now-controller.previous_time)*1e-6f,0.0f,.05f);
  controller.previous_time = now;
  OrbisPadData reports[64];
  const int count = scePadRead(controller.handle, reports, 64);
  if (count < 0) {
    controller.history = {};
    return true;
  }
  const uint32_t pressed = controller.history.consume(reports, size_t(count));
  if (pressed & ORBIS_PAD_BUTTON_OPTIONS) {
    std::puts("GNM_QUIT reason=options");
    return false;
  }
  const auto &data = controller.history.current;
  if (!data.connected)
    return true;
  input.orbit_x = -stick_axis(data.leftStick.x)*1.8f*dt;
  input.orbit_y = stick_axis(data.leftStick.y)*1.5f*dt;
  input.zoom = (float(data.analogButtons.r2)-float(data.analogButtons.l2))/255.0f*8.0f*dt;
  input.next_environment = (pressed & ORBIS_PAD_BUTTON_TRIANGLE) != 0;
  input.next_diagnostic = (pressed & ORBIS_PAD_BUTTON_SQUARE) != 0;
  input.reset = (pressed & ORBIS_PAD_BUTTON_CROSS) != 0;
  if (pressed & ORBIS_PAD_BUTTON_L1) input.exposure_stops -= .25f;
  if (pressed & ORBIS_PAD_BUTTON_R1) input.exposure_stops += .25f;
  return true;
}
}
static void log_message(void *, const char *s) {
  std::fprintf(stderr, "kstar_gfx: %s\n", s);
}
int main(int argc, char **argv) {
  try {
    auto options = demo::parse_options(argc, argv);
    if (options.help) {
      std::puts(demo::usage());
      return 0;
    }
    if (options.resources.empty())
      options.resources = "/app0/resources";
    // Headless runs remain independent of controller services and their state.
    // A capture is only produced when requested by the parsed command line.
    Controller controller;
    demo::Host host;
    if (!options.headless) {
      controller.open();
      host = {&controller, controller_pump, nullptr};
    }
    kgfx_gnm_config config{options.headless ? 0u : 1u, 2 * 1024 * 1024,
                           32 * 1024 * 1024};
    kgfx_device_desc desc{KGFX_ABI_VERSION, options.width, options.height, 3,
                          &config,          log_message,   nullptr};
    kgfx_device *device{};
    auto status = kgfx_device_create(&desc, &device);
    if (status) {
      std::fprintf(stderr, "Device creation failed: %d\n", status);
      return 1;
    }
    int result = demo::run(device, options, host);
    kgfx_device_destroy(device);
    return result;
  } catch (const std::exception &e) {
    std::fprintf(stderr, "kstar_gfx: %s\n", e.what());
    return 1;
  }
}
