/* Fuzzes frame transformation: vsTransformDataInit + vsTransformPrepare +
   vsDoTransform + vsTransformFinish with fuzzer-chosen geometry, pixel format,
   interpolator, border mode, lens correction and per-frame transforms.
   Source and destination share one frame size, as in ffmpeg's vidstabtransform:
   vsTransformPrepare/vsTransformFinish copy fiSrc-sized planes into buffers
   allocated for fiDest (transform.c), so the library requires equal sizes.
   Input layout: 16 config bytes, 2 x 12 transform bytes, then pixel bytes. */
#include "fuzz_common.h"

int LLVMFuzzerInitialize(int* argc, char*** argv){ (void)argc; (void)argv; vsfuzz_init(); return 0; }

int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size){
  if(size < 40) return -1;
  VsFuzzIn in = { data, 40 };
  int w = 1 + vf_u8(&in) % 128, h = 1 + vf_u8(&in) % 128;
  in.d += 2; in.n -= 2;                  /* reserved (formerly dest size) */
  VSPixelFormat pf = (VSPixelFormat)(vf_u8(&in) % PF_NUMBER);
  if(pf == PF_PACKED) pf = PF_BGR24;
  uint8_t b = vf_u8(&in);
  int inplace = (b >> 1) & 1;
  VSFrameInfo fis, fid;
  if(!vsFrameInfoInit(&fis, w, h, pf)) return -1;  /* frameinfo.h: invalid size for format */
  fid = fis;

  VSTransformConfig conf = vsTransformGetDefaultConfig("fuzz");
  conf.interpolType = (VSInterpolType)((b >> 2) % VS_NBInterPolTypes);
  conf.crop = (VSBorderType)((b >> 4) & 1);
  conf.lensCorrection = (VSLensCorrectMode)((b >> 5) % 3);
  b = vf_u8(&in);
  conf.optZoom = 0;
  conf.relative = 0;
  conf.smoothing = 0;
  conf.invert = b & 1;
  conf.zoom = vf_range(&in, -50, 50);
  conf.lensK = vf_range(&in, -0.6, 0.3);
  conf.fov = (b & 2) ? vf_range(&in, 1, 170) : 0;
  conf.maxShift = (b & 4) ? 1 + vf_u8(&in) : -1;
  conf.maxAngle = (b & 8) ? vf_range(&in, 0, 3.2) : -1;

  VSTransformData td;
  if(vsTransformDataInit(&td, &conf, &fis, &fid) != VS_OK) return 0;
  VSTransform t[2];
  in.d = data + 16; in.n = 24;
  for(int i = 0; i < 2; i++){
    t[i].x = vf_range(&in, -2.0 * w, 2.0 * w);
    t[i].y = vf_range(&in, -2.0 * h, 2.0 * h);
    t[i].alpha = vf_range(&in, -3.2, 3.2);
    t[i].zoom = vf_range(&in, -50, 100);
    t[i].barrel = vf_range(&in, -0.5, 0.5);
    t[i].rshutter = vf_range(&in, -0.5, 0.5);
    t[i].extra = 0;
  }
  VsFuzzIn px = { data + 40, size - 40 };
  VSFrame src, dst;
  vsFrameAllocate(&src, &fis);
  vsFrameAllocate(&dst, &fid);
  for(int i = 0; i < 2; i++){
    vf_fill_frame(&px, &src, &fis);
    if(vsTransformPrepare(&td, &src, inplace ? &src : &dst) != VS_OK) break;
    vsDoTransform(&td, t[i]);
    vsTransformFinish(&td);
  }
  vsFrameFree(&src);
  vsFrameFree(&dst);
  vsTransformDataCleanup(&td);
  return 0;
}
