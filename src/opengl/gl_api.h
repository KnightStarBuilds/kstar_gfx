#pragma once
#include "kstar_gfx/opengl.h"
#include <stddef.h>
#include <stdint.h>

#if defined(_WIN32)
#define KGFX_GL_CALL __stdcall
#else
#define KGFX_GL_CALL
#endif
using GLenum = unsigned;
using GLuint = unsigned;
using GLint = int;
using GLsizei = int;
using GLbitfield = unsigned;
using GLboolean = unsigned char;
using GLfloat = float;
using GLdouble = double;
using GLchar = char;
using GLubyte = unsigned char;
using GLintptr = ptrdiff_t;
using GLsizeiptr = ptrdiff_t;
using GLuint64 = uint64_t;
struct __GLsync;
using GLsync = __GLsync *;
enum : unsigned {
  GL_FALSE = 0,
  GL_TRUE = 1,
  GL_NO_ERROR = 0,
  GL_VERSION = 0x1F02,
  GL_RENDERER = 0x1F01,
  GL_MAJOR_VERSION = 0x821B,
  GL_MINOR_VERSION = 0x821C,
  GL_MAX_TEXTURE_SIZE = 0x0D33,
  GL_MAX_SAMPLES = 0x8D57,
  GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT = 0x8A34,
  GL_MAX_TEXTURE_IMAGE_UNITS = 0x8872,
  GL_ARRAY_BUFFER = 0x8892,
  GL_ELEMENT_ARRAY_BUFFER = 0x8893,
  GL_UNIFORM_BUFFER = 0x8A11,
  GL_PIXEL_PACK_BUFFER = 0x88EB,
  GL_DYNAMIC_DRAW = 0x88E8,
  GL_STATIC_DRAW = 0x88E4,
  GL_STREAM_READ = 0x88E1,
  GL_FLOAT = 0x1406,
  GL_HALF_FLOAT = 0x140B,
  GL_UNSIGNED_BYTE = 0x1401,
  GL_UNSIGNED_SHORT = 0x1403,
  GL_UNSIGNED_INT = 0x1405,
  GL_RGBA = 0x1908,
  GL_RED = 0x1903,
  GL_RGBA8 = 0x8058,
  GL_SRGB8_ALPHA8 = 0x8C43,
  GL_RGBA16F = 0x881A,
  GL_RGBA32F = 0x8814,
  GL_R32F = 0x822E,
  GL_DEPTH_COMPONENT = 0x1902,
  GL_DEPTH_COMPONENT32F = 0x8CAC,
  GL_TEXTURE_2D = 0x0DE1,
  GL_TEXTURE_CUBE_MAP = 0x8513,
  GL_TEXTURE_CUBE_MAP_POSITIVE_X = 0x8515,
  GL_TEXTURE_2D_MULTISAMPLE = 0x9100,
  GL_TEXTURE0 = 0x84C0,
  GL_TEXTURE_MIN_FILTER = 0x2801,
  GL_TEXTURE_MAG_FILTER = 0x2800,
  GL_TEXTURE_WRAP_S = 0x2802,
  GL_TEXTURE_WRAP_T = 0x2803,
  GL_TEXTURE_WRAP_R = 0x8072,
  GL_TEXTURE_MAX_LEVEL = 0x813D,
  GL_TEXTURE_BASE_LEVEL = 0x813C,
  GL_TEXTURE_COMPARE_MODE = 0x884C,
  GL_TEXTURE_COMPARE_FUNC = 0x884D,
  GL_COMPARE_REF_TO_TEXTURE = 0x884E,
  GL_NONE = 0,
  GL_NEAREST = 0x2600,
  GL_LINEAR = 0x2601,
  GL_LINEAR_MIPMAP_LINEAR = 0x2703,
  GL_NEAREST_MIPMAP_NEAREST = 0x2700,
  GL_CLAMP_TO_EDGE = 0x812F,
  GL_REPEAT = 0x2901,
  GL_UNPACK_ALIGNMENT = 0x0CF5,
  GL_UNPACK_ROW_LENGTH = 0x0CF2,
  GL_PACK_ALIGNMENT = 0x0D05,
  GL_PACK_ROW_LENGTH = 0x0D02,
  GL_FRAMEBUFFER = 0x8D40,
  GL_READ_FRAMEBUFFER = 0x8CA8,
  GL_DRAW_FRAMEBUFFER = 0x8CA9,
  GL_COLOR_ATTACHMENT0 = 0x8CE0,
  GL_DEPTH_ATTACHMENT = 0x8D00,
  GL_FRAMEBUFFER_COMPLETE = 0x8CD5,
  GL_FRAMEBUFFER_SRGB = 0x8DB9,
  GL_BACK = 0x0405,
  GL_FRONT = 0x0404,
  GL_FRONT_AND_BACK = 0x0408,
  GL_COLOR_BUFFER_BIT = 0x4000,
  GL_DEPTH_BUFFER_BIT = 0x0100,
  GL_VERTEX_SHADER = 0x8B31,
  GL_FRAGMENT_SHADER = 0x8B30,
  GL_COMPILE_STATUS = 0x8B81,
  GL_LINK_STATUS = 0x8B82,
  GL_INFO_LOG_LENGTH = 0x8B84,
  GL_INVALID_INDEX = 0xFFFFFFFFu,
  GL_DEPTH_TEST = 0x0B71,
  GL_BLEND = 0x0BE2,
  GL_CULL_FACE = 0x0B44,
  GL_SCISSOR_TEST = 0x0C11,
  GL_LESS = 0x0201,
  GL_LEQUAL = 0x0203,
  GL_ALWAYS = 0x0207,
  GL_CCW = 0x0901,
  GL_FILL = 0x1B02,
  GL_LINE = 0x1B01,
  GL_ONE = 1,
  GL_ZERO = 0,
  GL_SRC_ALPHA = 0x0302,
  GL_ONE_MINUS_SRC_ALPHA = 0x0303,
  GL_FUNC_ADD = 0x8006,
  GL_TRIANGLES = 4,
  GL_SYNC_GPU_COMMANDS_COMPLETE = 0x9117,
  GL_ALREADY_SIGNALED = 0x911A,
  GL_CONDITION_SATISFIED = 0x911C,
  GL_WAIT_FAILED = 0x911D,
  GL_SYNC_FLUSH_COMMANDS_BIT = 1,
  GL_MAP_READ_BIT = 1,
  GL_TEXTURE_CUBE_MAP_SEAMLESS = 0x884F
};
// clang-format off
#define KGFX_GL_FUNCTIONS(X) \
  X(const GLubyte *, GetString, (GLenum)) \
  X(void, GetIntegerv, (GLenum, GLint *)) \
  X(GLenum, GetError, (void)) \
  X(void, Enable, (GLenum)) \
  X(void, Disable, (GLenum)) \
  X(void, Viewport, (GLint, GLint, GLsizei, GLsizei)) \
  X(void, Scissor, (GLint, GLint, GLsizei, GLsizei)) \
  X(void, ClearColor, (GLfloat, GLfloat, GLfloat, GLfloat)) \
  X(void, ClearDepth, (GLdouble)) \
  X(void, Clear, (GLbitfield)) \
  X(void, DepthMask, (GLboolean)) \
  X(void, DepthFunc, (GLenum)) \
  X(void, ColorMask, (GLboolean, GLboolean, GLboolean, GLboolean)) \
  X(void, BlendFuncSeparate, (GLenum, GLenum, GLenum, GLenum)) \
  X(void, BlendEquation, (GLenum)) \
  X(void, CullFace, (GLenum)) \
  X(void, FrontFace, (GLenum)) \
  X(void, PolygonMode, (GLenum, GLenum)) \
  X(void, GenBuffers, (GLsizei, GLuint *)) \
  X(void, DeleteBuffers, (GLsizei, const GLuint *)) \
  X(void, BindBuffer, (GLenum, GLuint)) \
  X(void, BufferData, (GLenum, GLsizeiptr, const void *, GLenum)) \
  X(void, BufferSubData, (GLenum, GLintptr, GLsizeiptr, const void *)) \
  X(void, BindBufferRange, (GLenum, GLuint, GLuint, GLintptr, GLsizeiptr)) \
  X(void *, MapBufferRange, (GLenum, GLintptr, GLsizeiptr, GLbitfield)) \
  X(GLboolean, UnmapBuffer, (GLenum)) \
  X(void, GenTextures, (GLsizei, GLuint *)) \
  X(void, DeleteTextures, (GLsizei, const GLuint *)) \
  X(void, BindTexture, (GLenum, GLuint)) \
  X(void, ActiveTexture, (GLenum)) \
  X(void, TexParameteri, (GLenum, GLenum, GLint)) \
  X(void, TexImage2D, (GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void *)) \
  X(void, TexSubImage2D, (GLenum, GLint, GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, const void *)) \
  X(void, TexImage2DMultisample, (GLenum, GLsizei, GLenum, GLsizei, GLsizei, GLboolean)) \
  X(void, PixelStorei, (GLenum, GLint)) \
  X(void, GenSamplers, (GLsizei, GLuint *)) \
  X(void, DeleteSamplers, (GLsizei, const GLuint *)) \
  X(void, SamplerParameteri, (GLuint, GLenum, GLint)) \
  X(void, BindSampler, (GLuint, GLuint)) \
  X(void, GenVertexArrays, (GLsizei, GLuint *)) \
  X(void, DeleteVertexArrays, (GLsizei, const GLuint *)) \
  X(void, BindVertexArray, (GLuint)) \
  X(void, EnableVertexAttribArray, (GLuint)) \
  X(void, VertexAttribPointer, (GLuint, GLint, GLenum, GLboolean, GLsizei, const void *)) \
  X(void, VertexAttribDivisor, (GLuint, GLuint)) \
  X(GLuint, CreateShader, (GLenum)) \
  X(void, ShaderSource, (GLuint, GLsizei, const GLchar *const *, const GLint *)) \
  X(void, CompileShader, (GLuint)) \
  X(void, GetShaderiv, (GLuint, GLenum, GLint *)) \
  X(void, GetShaderInfoLog, (GLuint, GLsizei, GLsizei *, GLchar *)) \
  X(void, DeleteShader, (GLuint)) \
  X(GLuint, CreateProgram, (void)) \
  X(void, AttachShader, (GLuint, GLuint)) \
  X(void, LinkProgram, (GLuint)) \
  X(void, GetProgramiv, (GLuint, GLenum, GLint *)) \
  X(void, GetProgramInfoLog, (GLuint, GLsizei, GLsizei *, GLchar *)) \
  X(void, DeleteProgram, (GLuint)) \
  X(void, UseProgram, (GLuint)) \
  X(GLint, GetUniformLocation, (GLuint, const GLchar *)) \
  X(void, Uniform1i, (GLint, GLint)) \
  X(GLuint, GetUniformBlockIndex, (GLuint, const GLchar *)) \
  X(void, UniformBlockBinding, (GLuint, GLuint, GLuint)) \
  X(void, GenFramebuffers, (GLsizei, GLuint *)) \
  X(void, DeleteFramebuffers, (GLsizei, const GLuint *)) \
  X(void, BindFramebuffer, (GLenum, GLuint)) \
  X(void, FramebufferTexture2D, (GLenum, GLenum, GLenum, GLuint, GLint)) \
  X(GLenum, CheckFramebufferStatus, (GLenum)) \
  X(void, DrawBuffer, (GLenum)) \
  X(void, ReadBuffer, (GLenum)) \
  X(void, BlitFramebuffer, (GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLbitfield, GLenum)) \
  X(void, DrawArraysInstanced, (GLenum, GLint, GLsizei, GLsizei)) \
  X(void, DrawElementsInstanced, (GLenum, GLsizei, GLenum, const void *, GLsizei)) \
  X(void, ReadPixels, (GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void *)) \
  X(GLsync, FenceSync, (GLenum, GLbitfield)) \
  X(void, DeleteSync, (GLsync)) \
  X(GLenum, ClientWaitSync, (GLsync, GLbitfield, GLuint64)) \
  X(void, Flush, (void)) \
  X(void, Finish, (void))
// clang-format on

struct GlApi {
#define KGFX_GL_MEMBER(result, name, args)                                     \
  using name##Fn = result(KGFX_GL_CALL *) args;                                \
  name##Fn name{};
  KGFX_GL_FUNCTIONS(KGFX_GL_MEMBER)
#undef KGFX_GL_MEMBER
  bool load(kgfx_gl_get_proc_fn get, const char **missing) {
#define KGFX_GL_LOAD(result, name, args)                                       \
  name = reinterpret_cast<name##Fn>(get("gl" #name));                          \
  if (!name) {                                                                 \
    *missing = "gl" #name;                                                     \
    return false;                                                              \
  }
    KGFX_GL_FUNCTIONS(KGFX_GL_LOAD)
#undef KGFX_GL_LOAD
    return true;
  }
};
