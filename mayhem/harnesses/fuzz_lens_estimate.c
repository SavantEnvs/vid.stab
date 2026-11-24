/* Fuzzes the lens-distortion estimator over point correspondences:
   vsEstimateLensDistortionFromMatches, then vsLensFitSimilarity /
   vsLensMatchResiduals (and the rotational-model variants) at the returned k.
   Input layout: 12 config bytes, then per frame: count byte, optional mask
   flag, then 4 x int16 coordinates per match. */
#include <stdlib.h>
#include "fuzz_common.h"
#include "lensdistortion.h"

#define MAX_FRAMES 8
#define MAX_MATCHES 64

int LLVMFuzzerInitialize(int* argc, char*** argv){ (void)argc; (void)argv; vsfuzz_init(); return 0; }

int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size){
  if(size < 12) return -1;
  VsFuzzIn in = { data, size };
  int w = 2 * (1 + vf_u8(&in) % 512), h = 2 * (1 + vf_u8(&in) % 512);
  VSFrameInfo fi;
  if(!vsFrameInfoInit(&fi, w, h, PF_YUV420P)) return -1;
  VSLensEstimateConfig cfg = vsLensEstimateGetDefaultConfig();
  uint8_t b = vf_u8(&in);
  cfg.rejectOutliers = b & 1;
  cfg.outlierPasses = 1 + (b >> 1) % 4;
  cfg.gaussNewtonSteps = (b >> 3) % 6;
  cfg.maxIterations = 1 + vf_u8(&in) % 100;
  cfg.outlierStddevs = vf_range(&in, 0.5, 5);
  cfg.f = (vf_u8(&in) & 1) ? vf_range(&in, 10, 4000) : 0;
  int nframes = 1 + vf_u8(&in) % MAX_FRAMES;

  VSPointMatches frames[MAX_FRAMES];
  double* buf[MAX_FRAMES];
  unsigned char* masks[MAX_FRAMES];
  memset(buf, 0, sizeof(buf)); memset(masks, 0, sizeof(masks));
  for(int i = 0; i < nframes; i++){
    int n = vf_u8(&in) % (MAX_MATCHES + 1);
    int hasMask = vf_u8(&in) & 1;
    buf[i] = calloc(4 * (size_t)n + 1, sizeof(double));
    masks[i] = hasMask ? calloc((size_t)n + 1, 1) : NULL;
    for(int j = 0; j < n; j++){
      double px = (int16_t)vf_u16(&in) / 32768.0 * w, py = (int16_t)vf_u16(&in) / 32768.0 * h;
      double dx = (int8_t)vf_u8(&in) / 4.0, dy = (int8_t)vf_u8(&in) / 4.0;
      buf[i][j] = px; buf[i][n + j] = py;
      buf[i][2 * n + j] = px + dx; buf[i][3 * n + j] = py + dy;
      if(masks[i]) masks[i][j] = vf_u8(&in) & 1;
    }
    frames[i].px = buf[i]; frames[i].py = buf[i] + n;
    frames[i].qx = buf[i] + 2 * n; frames[i].qy = buf[i] + 3 * n;
    frames[i].active = masks[i];
    frames[i].n = n;
  }

  VSLensEstimate le = vsEstimateLensDistortionFromMatches(&fi, frames, nframes, &cfg);
  double k = (le.k == le.k && le.k > -1 && le.k < 1) ? le.k : 0;
  VSLensDistortion ld = vsLensDistortionInit(&fi, k);
  for(int i = 0; i < nframes; i++){
    VSTransform t; double res;
    double* r = calloc((size_t)frames[i].n + 1, sizeof(double));
    if(vsLensFitSimilarity(&ld, &frames[i], cfg.gaussNewtonSteps, &t, &res) == VS_OK)
      vsLensMatchResiduals(&ld, &frames[i], &t, r);
    if(cfg.f > 0 && vsLensFitSimilarityFov(&ld, &frames[i], cfg.gaussNewtonSteps, cfg.f, &t, &res) == VS_OK)
      vsLensMatchResidualsFov(&ld, &frames[i], &t, cfg.f, r);
    free(r);
  }
  for(int i = 0; i < nframes; i++){ free(buf[i]); free(masks[i]); }
  return 0;
}
