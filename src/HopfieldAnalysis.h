#ifndef HOPFIELDANALYSIS_H
#define HOPFIELDANALYSIS_H

#include "HopfieldContext.h"

#ifdef __cplusplus
extern "C" {
#endif

/* This bound is the requested ranking size, not a storage limit. */
#define HOPFIELD_TOP_MATCHES 3
typedef struct {
   const char *kind;
   int matchedPattern; /* 1-based; zero means none */
   int count;
   int indices[HOPFIELD_TOP_MATCHES];
   double overlaps[HOPFIELD_TOP_MATCHES];
} RecallAnalysis;

/* target is zero-based, or -1 when no clean target is known. Exact stored
   matches take precedence over inversions. Ties rank by input file order. */
RecallAnalysis analyzeRecall(const HopfieldContext *ctx,
                             const double output[], int target,
                             bool converged);
void printRecallAnalysis(const RecallAnalysis *analysis);

#ifdef __cplusplus
}
#endif
#endif
