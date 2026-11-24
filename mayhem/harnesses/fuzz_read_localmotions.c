/* Fuzzes the .trf local-motions reader (text + binary, auto-detected):
   vsReadLocalMotionsFile, the entry point the transform pass (ffmpeg
   vidstabtransform) uses on the file written by vidstabdetect. */
#include "fuzz_common.h"

int LLVMFuzzerInitialize(int* argc, char*** argv){ (void)argc; (void)argv; vsfuzz_init(); return 0; }

int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size){
  if(size == 0) return 0;               /* fmemopen rejects a zero-sized buffer */
  FILE* f = fmemopen((void*)data, size, "rb");
  if(!f) return 0;
  VSManyLocalMotions m;
  vs_vector_init(&m, 0);
  vsReadLocalMotionsFile(f, &m);
  fclose(f);
  for(int i = 0; i < vs_vector_size(&m); i++){
    LocalMotions* lm = (LocalMotions*)vs_vector_get(&m, i);
    if(lm) vs_vector_del(lm);
  }
  vs_vector_del(&m);
  return 0;
}
