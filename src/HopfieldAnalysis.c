#include "HopfieldAnalysis.h"
#include "HopfieldCalc.h"

#include <stdio.h>

RecallAnalysis analyzeRecall(const HopfieldContext *ctx,
                             const double output[], int target,
                             bool converged)
{
   RecallAnalysis result = {0};
   result.kind = converged ? "unknown" : "not_converged";
   if (!ctx || !output || !ctx->patterns || ctx->patternSize <= 0)
      return result;
   int exact = 0, inverse = 0;
   for (int p = 0; p < ctx->nPatterns; p++) {
      int distance = calcHammingDistance(ctx->patternSize,
                                        ctx->patterns[p], output);
      if (distance == 0 && (exact == 0 || p == target))
         exact = p + 1;
      if (distance == ctx->patternSize && inverse == 0)
         inverse = p + 1;
      double overlap = calcOverlap(ctx->patternSize, ctx->patterns[p],
                                   output);
      int position = 0;
      while (position < result.count &&
             (overlap < result.overlaps[position] ||
              equals(overlap, result.overlaps[position])))
         position++;
      if (position < HOPFIELD_TOP_MATCHES) {
         if (result.count < HOPFIELD_TOP_MATCHES)
            result.count++;
         for (int i = result.count - 1; i > position; i--) {
            result.indices[i] = result.indices[i - 1];
            result.overlaps[i] = result.overlaps[i - 1];
         }
         result.indices[position] = p + 1;
         result.overlaps[position] = overlap;
      }
   }
   if (converged && exact) {
      result.kind = target < 0 ? "stored" :
                    exact == target + 1 ? "correct" : "other";
      result.matchedPattern = exact;
   }
   else if (converged && inverse) {
      result.kind = "inverse";
      result.matchedPattern = inverse;
   }
   return result;
}

void printRecallAnalysis(const RecallAnalysis *analysis)
{
   printf("  Attractor: %s", analysis->kind);
   if (analysis->matchedPattern)
      printf(" (pattern %d)", analysis->matchedPattern);
   printf("; closest:");
   for (int i = 0; i < analysis->count; i++)
      printf(" %d:%.4f", analysis->indices[i], analysis->overlaps[i]);
   printf("\n");
}
