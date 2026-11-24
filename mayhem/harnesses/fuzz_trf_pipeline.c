/* Fuzzes the full transform pass as ffmpeg's vidstabtransform runs it:
   .trf file -> vsReadLocalMotionsFile -> vsLocalmotions2Transforms (incl. lens
   estimation) -> vsPreprocessTransforms (camera path: L1 / gaussian / avg,
   zoom) -> vsTransformPrepare/vsDoTransform/vsTransformFinish on a few frames.
   Input layout: 16 config bytes, then the .trf file. */
#include "fuzz_common.h"

/* The L1 camera path solves an LP over every frame; cap the clip length so a
   single input stays within a fuzzing exec budget (a resource cap, not a
   validity check: shorter clips reach the same code). */
#define MAX_FRAMES 64

int LLVMFuzzerInitialize(int* argc, char*** argv){ (void)argc; (void)argv; vsfuzz_init(); return 0; }

static void free_lms(VSManyLocalMotions* m){
  for(int i = 0; i < vs_vector_size(m); i++){
    LocalMotions* lm = (LocalMotions*)vs_vector_get(m, i);
    if(lm) vs_vector_del(lm);
  }
  vs_vector_del(m);
}

int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size){
  if(size < 17) return -1;
  VsFuzzIn in = { data, 16 };
  int w = 8 + vf_u8(&in) % 120, h = 8 + vf_u8(&in) % 120;
  VSPixelFormat pf = (VSPixelFormat)(vf_u8(&in) % PF_NUMBER);
  if(pf == PF_PACKED) pf = PF_RGB24;
  VSFrameInfo fi;
  if(!vsFrameInfoInit(&fi, w, h, pf)) return -1;  /* frameinfo.h: invalid size for format */

  VSTransformConfig conf = vsTransformGetDefaultConfig("fuzz");
  uint8_t b = vf_u8(&in);
  conf.camPathAlgo = (VSCamPathAlgo)(b % 3);
  conf.relative = (b >> 2) & 1;
  conf.invert = (b >> 3) & 1;
  conf.crop = (VSBorderType)((b >> 4) & 1);
  conf.simpleMotionCalculation = (b >> 5) & 1;
  conf.estimateLensDistortion = (b >> 6) & 1;
  conf.smoothZoom = (b >> 7) & 1;
  b = vf_u8(&in);
  conf.optZoom = b % 3;
  conf.interpolType = (VSInterpolType)((b >> 2) % VS_NBInterPolTypes);
  conf.lensCorrection = (VSLensCorrectMode)((b >> 4) % 3);
  conf.smoothing = vf_u8(&in) % 32;
  conf.zoom = vf_range(&in, -50, 50);
  conf.zoomSpeed = vf_range(&in, 0, 5);
  conf.lensK = vf_range(&in, -0.5, 0.3);
  conf.fov = (vf_u8(&in) & 1) ? vf_range(&in, 1, 170) : 0;
  conf.maxShift = (vf_u8(&in) & 1) ? 1 + vf_u8(&in) : -1;
  conf.storeTransforms = 0;     /* would write global_motions.trf into the cwd */
  conf.verbose = 0;

  FILE* f = fmemopen((void*)(data + 16), size - 16, "rb");
  if(!f) return 0;
  VSManyLocalMotions m;
  vs_vector_init(&m, 0);
  int rc = vsReadLocalMotionsFile(f, &m);
  fclose(f);
  if(rc != VS_OK || vs_vector_size(&m) == 0 || vs_vector_size(&m) > MAX_FRAMES){
    free_lms(&m);
    return 0;
  }

  VSTransformData td;
  if(vsTransformDataInit(&td, &conf, &fi, &fi) != VS_OK){ free_lms(&m); return 0; }
  VSTransformations trans;
  vsTransformationsInit(&trans);
  if(vsLocalmotions2Transforms(&td, &m, &trans) == VS_OK &&
     vsPreprocessTransforms(&td, &trans) == VS_OK){
    VSFrame src, dst;
    vsFrameAllocate(&src, &fi);
    vsFrameAllocate(&dst, &fi);
    VsFuzzIn px = { data + 16, size - 16 };
    for(int i = 0; i < 3 && i < trans.len; i++){
      vf_fill_frame(&px, &src, &fi);
      if(vsTransformPrepare(&td, &src, (i & 1) ? &src : &dst) != VS_OK) break;
      vsDoTransform(&td, vsGetNextTransform(&td, &trans));
      vsTransformFinish(&td);
    }
    vsFrameFree(&src);
    vsFrameFree(&dst);
  }
  vsTransformationsCleanup(&trans);
  vsTransformDataCleanup(&td);
  free_lms(&m);
  return 0;
}
