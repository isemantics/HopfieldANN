#ifndef HOPFIELDRECORD_H
#define HOPFIELDRECORD_H

#include "HopfieldAnalysis.h"
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Streaming JSONL: no trace buffers are retained by the CLI. All calls
   tolerate NULL streams. The owner checks ferror and fclose on completion. */
void recordSession(FILE *file, const HopfieldContext *ctx, unsigned seed);
void recordStart(FILE *file, size_t id, const HopfieldContext *ctx,
                 const char *rule, int noise, const char *corruption,
                 int trial, int pattern, bool noisyReference,
                 const double reference[], const double input[],
                 double trainingSeconds);
void recordIteration(FILE *file, size_t id, int iteration, double energy,
                     const double pattern[], int size);
void recordResult(FILE *file, size_t id, const double output[], int size,
                  bool converged, double energy, double overlap,
                  int hamming, double recallSeconds,
                  const RecallAnalysis *analysis);
void recordFailure(FILE *file, const char *rule, int stored, int noise,
                   int trial, const char *corruption);
void recordEnd(FILE *file, int exitCode);

#ifdef __cplusplus
}
#endif
#endif
