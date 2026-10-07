#include "HopfieldNoise.h"
#include "HopfieldCalc.h"
#include "HopfieldUtil.h"

#include <math.h>

int corruptPattern(HopfieldContext *ctx, int patternIndex, int percent,
                   HopfieldNoise noise, double output[])
{
   if (!ctx || !output || !ctx->patterns || ctx->nRows <= 0 ||
       ctx->nColumns <= 0 || patternIndex < 0 ||
       patternIndex >= ctx->nPatterns || percent < 0 || percent > 100 ||
       noise < HOPFIELD_NOISE_FLIP || noise > HOPFIELD_NOISE_BOTTOM)
      return -1;

   const double *original = ctx->patterns[patternIndex];
   if (noise == HOPFIELD_NOISE_FLIP || noise == HOPFIELD_NOISE_ERASE) {
      int affected = addNoiseToPattern(ctx, patternIndex, percent);
      if (affected < 0)
         return -1;
      for (int i = 0; i < ctx->patternSize; i++) {
         double value = ctx->noisyPatterns[patternIndex][i];
         output[i] = noise == HOPFIELD_NOISE_ERASE &&
                     !equals(value, original[i]) ? 0.0 : value;
      }
      return affected;
   }

   copyPattern(ctx->patternSize, original, output);
   int height = ctx->nRows;
   int width = ctx->nColumns;
   int row = 0;
   int column = 0;
   if (noise == HOPFIELD_NOISE_BLOCK) {
      int target = (int)((long long)ctx->patternSize * percent / 100);
      if (target == 0)
         return 0;
      /* Approximate the image aspect ratio, then round to whole pixels.
         The caller reports actual area, which can differ from percent. */
      double scale = sqrt((double)target / ctx->patternSize);
      height = (int)floor(ctx->nRows * scale + 0.5);
      if (height < 1)
         height = 1;
      width = (int)floor((double)target / height + 0.5);
      if (width < 1)
         width = 1;
      if (width > ctx->nColumns)
         width = ctx->nColumns;
      row = randomInt(0, ctx->nRows - height);
      column = randomInt(0, ctx->nColumns - width);
   }
   else if (noise == HOPFIELD_NOISE_LEFT ||
            noise == HOPFIELD_NOISE_RIGHT) {
      width = (int)((long long)width * percent / 100);
      if (noise == HOPFIELD_NOISE_RIGHT)
         column = ctx->nColumns - width;
   }
   else {
      height = (int)((long long)height * percent / 100);
      if (noise == HOPFIELD_NOISE_BOTTOM)
         row = ctx->nRows - height;
   }
   for (int r = row; r < row + height; r++) {
      for (int c = column; c < column + width; c++)
         output[r * ctx->nColumns + c] = 0.0;
   }
   return height * width;
}
