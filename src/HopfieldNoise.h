#ifndef HOPFIELDNOISE_H
#define HOPFIELDNOISE_H

#include "HopfieldContext.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
   HOPFIELD_NOISE_FLIP,
   HOPFIELD_NOISE_ERASE,
   HOPFIELD_NOISE_BLOCK,
   HOPFIELD_NOISE_LEFT,
   HOPFIELD_NOISE_RIGHT,
   HOPFIELD_NOISE_TOP,
   HOPFIELD_NOISE_BOTTOM
} HopfieldNoise;

/* Copy a stored pattern and corrupt it. Erasures are neutral zeros;
   stored memories stay binary. Percent must be 0..100. Output must have
   patternSize elements and must not alias context-owned pattern storage.
   Returns affected pixel count, or -1 on invalid input/allocation failure. */
int corruptPattern(HopfieldContext *ctx, int patternIndex, int percent,
                   HopfieldNoise noise, double output[]);

#ifdef __cplusplus
}
#endif

#endif
