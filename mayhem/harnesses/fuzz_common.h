/* Shared helpers for the vid.stab fuzz harnesses (mayhem/harnesses). */
#ifndef VS_FUZZ_COMMON_H
#define VS_FUZZ_COMMON_H
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include "libvidstab.h"

/* The library logs every rejected line/frame to stderr; silence it so runs stay
   fast. Set once in LLVMFuzzerInitialize and never changed afterwards. */
static int vsfuzz_nolog(int type, const char* tag, const char* format, ...){
  (void)type; (void)tag; (void)format; return 0;
}
static void vsfuzz_init(void){ vs_log = vsfuzz_nolog; }

typedef struct { const uint8_t* d; size_t n; } VsFuzzIn;

static uint8_t vf_u8(VsFuzzIn* in){
  if(in->n == 0) return 0;
  uint8_t v = in->d[0]; in->d++; in->n--; return v;
}
static uint16_t vf_u16(VsFuzzIn* in){ uint16_t a = vf_u8(in); return (uint16_t)(a | (vf_u8(in) << 8)); }
/* a finite double in [lo,hi] (configs with NaN/Inf are caller error under -ffast-math) */
static double vf_range(VsFuzzIn* in, double lo, double hi){
  return lo + (hi - lo) * (vf_u16(in) / 65535.0);
}
/* fill every plane of an allocated frame from the remaining bytes (cycled) */
static void vf_fill_frame(VsFuzzIn* in, VSFrame* f, const VSFrameInfo* fi){
  int p;
  int planes = fi->pFormat < PF_PACKED ? fi->planes : 1;
  for(p = 0; p < planes; p++){
    int h = fi->pFormat < PF_PACKED ? (fi->height >> vsGetPlaneHeightSubS(fi, p)) : fi->height;
    size_t sz = (size_t)f->linesize[p] * (size_t)h, i;
    if(!f->data[p]) continue;
    for(i = 0; i < sz; i++)
      f->data[p][i] = in->n ? in->d[i % in->n] ^ (uint8_t)(i >> 8) : (uint8_t)(i * 7 + p);
  }
}
#endif
