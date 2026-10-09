#include <gnm_depthrendertarget.h>
#include <gnm_helpers.h>
#include <gpuaddr.h>
#include <stdio.h>
#include <stdlib.h>

#define CHECK(x)                                                               \
  do {                                                                         \
    if (!(x)) {                                                                \
      fprintf(stderr, "layout check failed at %d: %s\n", __LINE__, #x);        \
      return 1;                                                                \
    }                                                                          \
  } while (0)

// Detect sample aliasing and out-of-range addresses, not just successful
// creation.
static int check_texture(GnmDataFormat format, unsigned bpp, unsigned w,
                         unsigned h, unsigned samples, int depth) {
  printf("Checking %ux%u bpp%u samples%u depth%d\n", w,h,bpp,samples,depth);
  const GnmTileMode tile = depth         ? GNM_TM_DEPTH_2D_THIN_256
                           : samples > 1 ? GNM_TM_THIN_2D_THIN
                                         : GNM_TM_DISPLAY_LINEAR_ALIGNED;
  GnmTextureCreateInfo ci = {0};
  sceGnmTexInit2dCreateInfo(&ci, format, w, h, 1, tile, GNM_GPU_BASE);
  if (samples > 1) {
    ci.texturetype = GNM_TEXTURE_2D_MSAA;
    ci.numfragments = samples;
  }
  GnmTexture texture = {0};
  CHECK(sceGnmCreateTexture(&texture, &ci) == GNM_ERROR_OK);
  uint64_t bytes = 0;
  uint32_t alignment = 0;
  CHECK(sceGnmTexCalcByteSize(&bytes, &alignment, &texture) == GNM_ERROR_OK);
  CHECK(bytes >= (uint64_t)w * h * (bpp / 8) * samples);
  CHECK(alignment && (alignment & (alignment - 1)) == 0);
  CHECK(sceGnmTexGetNumFragments(&texture) == samples);
  GpaTextureInfo info = sceGnmTexBuildInfo(&texture);
  GpaTilingParams tp = {0};
  GpaSurfaceContext context = {0};
  CHECK(sceGpaTpInit(&tp, &info, 0, 0) == GPA_ERR_OK);
  CHECK(sceGpaInitSurfaceContext(&context, bytes, &tp) == GPA_ERR_OK);
  uint64_t invalid_offset = 0;
  CHECK(sceGpaComputeSurfaceCoord(&invalid_offset, NULL, NULL, 0, 0, 0, 0) ==
        GPA_ERR_INVALID_ARGS);
  CHECK(sceGpaComputeSurfaceCoord(&invalid_offset, NULL, &context,
                                 context.paddedwidth, 0, 0, 0) ==
        GPA_ERR_INVALID_ARGS);
  CHECK(sceGpaComputeSurfaceCoord(&invalid_offset, NULL, &context, 0,
                                 context.paddedheight, 0, 0) ==
        GPA_ERR_INVALID_ARGS);
  CHECK(sceGpaComputeSurfaceCoord(&invalid_offset, NULL, &context, 0, 0,
                                 context.paddeddepth, 0) == GPA_ERR_INVALID_ARGS);
  CHECK(sceGpaComputeSurfaceCoord(&invalid_offset, NULL, &context, 0, 0, 0,
                                 context.numfragsperpixel) ==
        GPA_ERR_INVALID_ARGS);
  unsigned char *used = calloc((size_t)bytes, 1);
  CHECK(used);
  for (unsigned y = 0; y < h; ++y)
    for (unsigned x = 0; x < w; ++x)
      for (unsigned sample = 0; sample < samples; ++sample) {
        uint64_t at = 0, bit = 0;
        CHECK(sceGpaComputeSurfaceCoord(&at, &bit, &context, x, y, 0, sample) ==
              GPA_ERR_OK);
        CHECK(bit == 0 && at + bpp / 8 <= bytes);
        for (unsigned c = 0; c < bpp / 8; ++c) {
          if (used[at+c]) fprintf(stderr,"Alias at x%u y%u sample%u byte%llu\n",x,y,sample,(unsigned long long)(at+c));
        CHECK(used[at + c] == 0);
          used[at + c] = 1;
        }
      }
  free(used);
  if (depth) {
    GnmDepthRenderTargetCreateInfo di = {0};
    di.width = w;
    di.height = h;
    di.numslices = 1;
    di.zfmt = GNM_Z_32_FLOAT;
    di.tilemodehint = tile;
    di.mingpumode = GNM_GPU_BASE;
    di.numfragments = samples;
    di.flags.enable_htile_acceleration = 1;
    GnmDepthRenderTarget target = {0};
    CHECK(sceGnmCreateDepthRenderTarget(&target, &di) == GNM_ERROR_OK);
    uint64_t target_bytes = 0;
    uint32_t target_alignment = 0;
    CHECK(sceGnmDrtCalcByteSize(&target_bytes, &target_alignment, &target) ==
          GNM_ERROR_OK);
    CHECK(target_bytes == bytes);
    CHECK(sceGnmDrtGetPaddedWidth(&target) == sceGnmTexGetPitch(&texture));
    GpaHtileParams hp = {0};
    GpaHtileInfo hi = {0};
    hp.pitch = sceGnmDrtGetPaddedWidth(&target);
    hp.height = sceGnmDrtGetPaddedHeight(&target);
    hp.numslices = 1;
    hp.numfrags = samples;
    hp.bpp = 32;
    hp.arraymode = target.depthinfo.arraymode;
    hp.banks = target.depthinfo.numbanks;
    hp.pipeconfig = target.depthinfo.pipeconfig;
    hp.mingpumode = GNM_GPU_BASE;
    CHECK(sceGpaComputeHtileInfo(&hi, &hp) == GPA_ERR_OK);
    CHECK(hi.htilebytes > 0 && hi.basealign > 0);
  }
  return 0;
}

int main(void) {
  const GnmDataFormat formats[] = {
      GNM_FMT_R8G8B8A8_UNORM, GNM_FMT_R16G16B16A16_FLOAT,
      GNM_FMT_R32G32B32A32_FLOAT, GNM_FMT_R32_FLOAT};
  const unsigned bits[] = {32, 64, 128, 32};
  const unsigned sizes[][2] = {{128, 128}, {320, 180}, {17, 31}};
  for (unsigned f = 0; f < 4; ++f)
    for (unsigned size = 0; size < 3; ++size)
      for (unsigned samples = 1; samples <= 4; samples += 3)
        CHECK(check_texture(formats[f], bits[f], sizes[size][0], sizes[size][1],
                            samples, 0) == 0);
  for (unsigned size = 0; size < 3; ++size)
    for (unsigned samples = 1; samples <= 4; samples += 3)
      CHECK(check_texture(GNM_FMT_R32_FLOAT, 32, sizes[size][0], sizes[size][1],
                          samples, 1) == 0);
  GnmTextureCreateInfo ci = {0};
  sceGnmTexInit2dCreateInfo(&ci, GNM_FMT_R16G16B16A16_FLOAT, 32, 32, 6,
                            GNM_TM_DISPLAY_LINEAR_ALIGNED, GNM_GPU_BASE);
  ci.texturetype = GNM_TEXTURE_CUBEMAP;
  ci.numslices = 1;
  GnmTexture cube = {0};
  CHECK(sceGnmCreateTexture(&cube, &ci) == GNM_ERROR_OK);
  CHECK(cube.lastarray == 5 && cube.lastlevel == 5);
  GpaTextureInfo info = sceGnmTexBuildInfo(&cube);
  uint64_t total = 0;
  uint32_t alignment = 0;
  CHECK(sceGnmTexCalcByteSize(&total, &alignment, &cube) == GNM_ERROR_OK);
  for (unsigned face = 0; face < 6; ++face)
    for (unsigned mip = 0; mip < 6; ++mip) {
      uint64_t size = 0, offset = 0;
      CHECK(sceGpaComputeSurfaceSizeOffset(&size, &offset, &info, mip, face) ==
            GPA_ERR_OK);
      CHECK(size > 0 && offset + size <= total);
    }
  puts("GNM host layout: all color/depth samples have unique bounded "
       "addresses; cube and HTILE layouts passed");
  return 0;
}
