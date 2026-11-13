// SPDX-License-Identifier: MIT
// Exercise asynchronous query ownership without a window or a GPU. The native
// conformance test separately verifies timestamps around actual rendering.
#include "../src/opengl/gl_api.h"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <type_traits>

#define CHECK(expression)                                                     \
  do {                                                                        \
    if (!(expression)) {                                                      \
      std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #expression);\
      std::exit(1);                                                           \
    }                                                                         \
  } while (0)

namespace {
template <typename> struct Noop;
template <typename Result, typename... Args>
struct Noop<Result(KGFX_GL_CALL *)(Args...)> {
  static Result KGFX_GL_CALL call(Args...) {
    if constexpr (!std::is_void_v<Result>)
      return {};
  }
};
struct Query {
  GLuint64 timestamp{};
  bool available{};
};
std::array<Query, 32> queries{};
bool timing_procs = true, completed = false;
GLint counter_bits = 30;
GLuint next_query = 1;
unsigned result_reads{}, deletes{}, finishes{}, flushes{}, waits{};
GLuint64 clock_ns{};

void KGFX_GL_CALL get_integer(GLenum name, GLint *out) {
  switch (name) {
  case GL_MAJOR_VERSION: *out = 4; break;
  case GL_MINOR_VERSION: *out = 1; break;
  case GL_MAX_TEXTURE_SIZE: *out = 4096; break;
  case GL_MAX_SAMPLES: *out = 4; break;
  case GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT: *out = 256; break;
  case GL_MAX_TEXTURE_IMAGE_UNITS: *out = 16; break;
  default: CHECK(false);
  }
}
void KGFX_GL_CALL get_query(GLenum target, GLenum name, GLint *out) {
  CHECK(target == GL_TIMESTAMP && name == GL_QUERY_COUNTER_BITS);
  *out = counter_bits;
}
void KGFX_GL_CALL gen_queries(GLsizei count, GLuint *out) {
  for (GLsizei i = 0; i < count; ++i) {
    CHECK(next_query < queries.size());
    out[i] = next_query++;
  }
}
void KGFX_GL_CALL delete_queries(GLsizei count, const GLuint *) {
  deletes += count;
}
void KGFX_GL_CALL query_counter(GLuint id, GLenum target) {
  CHECK(target == GL_TIMESTAMP && id && id < next_query);
  queries[id] = {clock_ns, false};
  clock_ns = (clock_ns + 120) & ((uint64_t{1} << counter_bits) - 1);
}
void KGFX_GL_CALL query_available(GLuint id, GLenum name, GLint *out) {
  CHECK(name == GL_QUERY_RESULT_AVAILABLE && id && id < next_query);
  *out = queries[id].available;
}
void KGFX_GL_CALL query_result(GLuint id, GLenum name, GLuint64 *out) {
  CHECK(name == GL_QUERY_RESULT && id && id < next_query);
  // Requesting a result before availability would synchronously stall GL.
  CHECK(queries[id].available);
  ++result_reads;
  *out = queries[id].timestamp;
}
void KGFX_GL_CALL finish() { ++finishes; }
void KGFX_GL_CALL flush() { ++flushes; }
GLsync KGFX_GL_CALL fence(GLenum, GLbitfield) {
  return reinterpret_cast<GLsync>(uintptr_t{1});
}
GLenum KGFX_GL_CALL client_wait(GLsync, GLbitfield flags, GLuint64 timeout) {
  CHECK(flags == 0 && timeout == 0);
  ++waits;
  return completed ? GL_ALREADY_SIGNALED : 0;
}
void *get_proc(const char *name) {
#define PROC(symbol, function) \
  if (std::strcmp(name, "gl" #symbol) == 0) \
    return reinterpret_cast<void *>(function);
  PROC(GetIntegerv, get_integer)
  PROC(Finish, finish)
  PROC(Flush, flush)
  PROC(FenceSync, fence)
  PROC(ClientWaitSync, client_wait)
  if (timing_procs) {
    PROC(GetQueryiv, get_query)
    PROC(GenQueries, gen_queries)
    PROC(DeleteQueries, delete_queries)
    PROC(QueryCounter, query_counter)
    PROC(GetQueryObjectiv, query_available)
    PROC(GetQueryObjectui64v, query_result)
  }
#undef PROC
#define FALLBACK(result, symbol, args) \
  if (std::strcmp(name, "gl" #symbol) == 0) \
    return reinterpret_cast<void *>(&Noop<GlApi::symbol##Fn>::call);
  KGFX_GL_FUNCTIONS(FALLBACK)
#undef FALLBACK
  return nullptr;
}
kgfx_device *device() {
  kgfx_gl_config config{get_proc, nullptr, nullptr};
  kgfx_device_desc desc{KGFX_ABI_VERSION, 64, 64, 3, &config, nullptr, nullptr};
  kgfx_device *result{};
  CHECK(kgfx_device_create(&desc, &result) == KGFX_OK);
  return result;
}
} // namespace

int main() {
  for (const bool procs : {false, true}) {
    timing_procs = procs;
    counter_bits = 0;
    auto *d = device();
    kgfx_gpu_timer *timer = reinterpret_cast<kgfx_gpu_timer *>(uintptr_t{1});
    CHECK(kgfx_gpu_timer_create(d, &timer) == KGFX_UNSUPPORTED && !timer);
    kgfx_device_destroy(d);
  }
  counter_bits = 30;
  auto *d = device();
  auto *other = device();
  kgfx_gpu_timer *timer{};
  CHECK(kgfx_gpu_timer_create(d, &timer) == KGFX_OK);
  kgfx_gpu_timer_result result{19, 31};
  CHECK(kgfx_gpu_timer_read(d, timer, &result) == KGFX_NOT_READY);
  CHECK(result.frame_serial == 19 && result.elapsed_ns == 31);
  CHECK(kgfx_gpu_timer_read(other, timer, &result) == KGFX_INVALID_ARGUMENT);
  CHECK(kgfx_gpu_timer_begin(d, timer) == KGFX_INVALID_ARGUMENT);
  CHECK(kgfx_frame_begin(d) == KGFX_OK);
  CHECK(kgfx_gpu_timer_end(d, timer) == KGFX_INVALID_ARGUMENT);
  clock_ns = (uint64_t{1} << counter_bits) - 50;
  CHECK(kgfx_gpu_timer_begin(d, timer) == KGFX_OK);
  CHECK(kgfx_gpu_timer_begin(d, timer) == KGFX_INVALID_ARGUMENT);
  CHECK(kgfx_gpu_timer_read(d, timer, &result) == KGFX_NOT_READY);
  CHECK(kgfx_gpu_timer_end(d, timer) == KGFX_OK);
  CHECK(kgfx_gpu_timer_begin(d, timer) == KGFX_NOT_READY);
  CHECK(kgfx_gpu_timer_end(d, timer) == KGFX_INVALID_ARGUMENT);
  const auto old_waits = waits, old_finishes = finishes, old_flushes = flushes;
  queries[1].available = true;
  CHECK(kgfx_gpu_timer_read(d, timer, &result) == KGFX_NOT_READY);
  CHECK(result.frame_serial == 19 && result.elapsed_ns == 31);
  CHECK(result_reads == 0);
  queries[2].available = true;
  CHECK(kgfx_gpu_timer_read(d, timer, &result) == KGFX_OK);
  CHECK(result.frame_serial == 1 && result.elapsed_ns == 120);
  CHECK(result_reads == 2);
  CHECK(kgfx_gpu_timer_read(d, timer, &result) == KGFX_OK && result_reads == 2);
  CHECK(old_waits == waits && old_finishes == finishes && old_flushes == flushes);
  CHECK(kgfx_gpu_timer_begin(d, timer) == KGFX_OK);
  CHECK(kgfx_gpu_timer_end(d, timer) == KGFX_OK);
  kgfx_gpu_timer_destroy(d, timer);
  CHECK(deletes == 0);
  const auto serial = kgfx_submit(d, 0);
  CHECK(serial == 1 && deletes == 0);
  completed = true;
  CHECK(kgfx_is_complete(d, serial));
  CHECK(deletes == 2);
  kgfx_stats stats{};
  kgfx_get_stats(d, &stats);
  CHECK(stats.steady_state_allocations == 0);

  // A forgotten end cannot silently include commands from a subsequent frame.
  CHECK(kgfx_gpu_timer_create(d, &timer) == KGFX_OK);
  CHECK(kgfx_frame_begin(d) == KGFX_OK);
  CHECK(kgfx_gpu_timer_begin(d, timer) == KGFX_OK);
  CHECK(kgfx_submit(d, 0) == 2);
  CHECK(kgfx_frame_begin(d) == KGFX_OK);
  CHECK(kgfx_gpu_timer_end(d, timer) == KGFX_INVALID_ARGUMENT);
  kgfx_gpu_timer_destroy(d, timer);
  CHECK(kgfx_submit(d, 0) == 3);
  CHECK(deletes == 4);
  // Device ownership also releases a timer that the caller leaves alive.
  CHECK(kgfx_gpu_timer_create(d, &timer) == KGFX_OK);
  kgfx_device_destroy(d);
  kgfx_device_destroy(other);
  CHECK(deletes == 6);
  std::puts("PASS: optional GPU timers, nonblocking reads, counter wrap, ownership");
}
