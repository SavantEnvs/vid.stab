/* Fuzzes motion detection (the vidstabdetect pass) on fuzzer-made frames of any
   pixel format, including odd sizes and the packed RGB24/BGR24/RGBA paths, then
   serializes the result with vsPrepareFile/vsWriteToFile.
   Input layout: 10 config bytes, then frame pixel bytes. */
#include "fuzz_common.h"

int LLVMFuzzerInitialize(int* argc, char*** argv){ (void)argc; (void)argv; vsfuzz_init(); return 0; }

int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size){
  if(size < 10) return -1;
  VsFuzzIn in = { data, 10 };
  int w = 1 + vf_u8(&in) % 128, h = 1 + vf_u8(&in) % 128;
  VSPixelFormat pf = (VSPixelFormat)(vf_u8(&in) % PF_NUMBER);
  if(pf == PF_PACKED) pf = PF_RGBA;
  VSFrameInfo fi;
  if(!vsFrameInfoInit(&fi, w, h, pf)) return -1;  /* frameinfo.h: invalid size for format */

  VSMotionDetectConfig conf = vsMotionDetectGetDefaultConfig("fuzz");
  uint8_t b = vf_u8(&in);
  conf.shakiness = 1 + b % 10;
  conf.accuracy = 1 + (b >> 4) % 15;
  b = vf_u8(&in);
  conf.stepSize = 1 + b % 32;
  b = vf_u8(&in);
  conf.virtualTripod = b % 4;
  conf.show = (b >> 2) % 3;
  int nframes = 2 + ((b >> 4) & 1);
  int mode = (b >> 5) & 1 ? BINARY_SERIALIZATION_MODE : ASCII_SERIALIZATION_MODE;
  conf.contrastThreshold = vf_range(&in, 0, 1);
  conf.numThreads = 1;
  in.d = data + 10; in.n = size - 10;

  VSMotionDetect md;
  if(vsMotionDetectInit(&md, &conf, &fi) != VS_OK) return 0;
  md.serializationMode = mode;
  FILE* out = tmpfile();
  if(out) vsPrepareFile(&md, out);
  VSFrame frame;
  vsFrameAllocate(&frame, &fi);
  for(int i = 0; i < nframes; i++){
    LocalMotions lms;
    vs_vector_init(&lms, 0);
    vf_fill_frame(&in, &frame, &fi);
    /* rotate the input so successive frames differ */
    if(in.n > 1){ size_t s = 1 + (in.d[0] % (in.n - 1)); in.d += s; in.n -= s; }
    if(vsMotionDetection(&md, &lms, &frame) == VS_OK && out)
      vsWriteToFile(&md, out, &lms);
    vs_vector_del(&lms);
  }
  vsFrameFree(&frame);
  if(out) fclose(out);
  vsMotionDetectionCleanup(&md);
  return 0;
}
