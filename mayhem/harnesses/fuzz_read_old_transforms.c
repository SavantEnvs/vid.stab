/* Fuzzes the deprecated plain-text transforms reader vsReadOldTransforms and the
   smoothing pass that consumes its result (vsPreprocessTransforms). */
#include "fuzz_common.h"

int LLVMFuzzerInitialize(int* argc, char*** argv){ (void)argc; (void)argv; vsfuzz_init(); return 0; }

int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size){
  if(size == 0) return 0;
  VSFrameInfo fi;
  if(!vsFrameInfoInit(&fi, 64, 64, PF_YUV420P)) return 0;
  VSTransformConfig conf = vsTransformGetDefaultConfig("fuzz");
  conf.camPathAlgo = VSGaussian;
  VSTransformData td;
  if(vsTransformDataInit(&td, &conf, &fi, &fi) != VS_OK) return 0;
  FILE* f = fmemopen((void*)data, size, "rb");
  if(!f){ vsTransformDataCleanup(&td); return 0; }
  VSTransformations trans;
  vsTransformationsInit(&trans);
  if(vsReadOldTransforms(&td, f, &trans) > 0 && trans.len > 0)
    vsPreprocessTransforms(&td, &trans);
  fclose(f);
  vsTransformationsCleanup(&trans);
  vsTransformDataCleanup(&td);
  return 0;
}
