// Demo process termination uses the console service after normal libc cleanup.
// OpenOrbis musl's default _exit issues a syscall rejected by retail firmware.
#include <orbis/Sysmodule.h>
#include <orbis/SystemService.h>
#include <orbis/libkernel.h>
#include <cerrno>
#include <cstdint>
#include <ctime>

// OpenOrbis libc++ uses Linux clock IDs; firmware uses BSD IDs. Monotonic
// elapsed time is native clock 4, while native clock 1 measures CPU time.
extern "C" int32_t kgfx_kernel_clock_gettime(int, timespec *)
    __asm__("sceKernelClockGettime");
extern "C" int clock_gettime(clockid_t clock, timespec *output) {
  if (!output) {
    errno = EFAULT;
    return -1;
  }
  int native;
  switch (clock) {
  case CLOCK_REALTIME: native = 0; break;
  case CLOCK_MONOTONIC: native = 4; break;
  default: errno = EINVAL; return -1;
  }
  const int32_t result = kgfx_kernel_clock_gettime(native, output);
  if (result >= 0)
    return 0;
  const uint32_t code = uint32_t(result);
  errno = (code & 0xffff0000u) == 0x80020000u ? int(code & 0xffffu) : EIO;
  return -1;
}

extern "C" [[noreturn]] void _exit(int) {
  sceSysmoduleLoadModuleInternal(ORBIS_SYSMODULE_INTERNAL_SYSTEM_SERVICE);
  sceSystemServiceLoadExec("exit", nullptr);
  for (;;)
    sceKernelUsleep(1000000);
}
extern "C" [[noreturn]] void _Exit(int status) { _exit(status); }
