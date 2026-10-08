#include "HopfieldRecord.h"
#include "AppInfo.h"
#include "HopfieldUtil.h"

#include <math.h>

static void number(FILE *file, double value)
{
   if (isfinite(value))
      fprintf(file, "%.17g", value);
   else
      fputs("null", file);
}

static void raster(FILE *file, const double values[], int size)
{
   fputc('[', file);
   for (int i = 0; i < size; i++) {
      if (i)
         fputc(',', file);
      number(file, values[i]);
   }
   fputc(']', file);
}

void recordSession(FILE *file, const HopfieldContext *ctx, unsigned seed)
{
   if (!file)
      return;
   fprintf(file, "{\"type\":\"session\",\"format\":1,\"version\":\"%s\","
                 "\"seed\":%u,\"rows\":%d,\"columns\":%d,\"memories\":[",
           VERSION, seed, ctx->nRows, ctx->nColumns);
   for (int i = 0; i < ctx->nPatterns; i++) {
      if (i)
         fputc(',', file);
      raster(file, ctx->patterns[i], ctx->patternSize);
   }
   fputs("]}\n", file);
}

/* All string arguments are internal enum labels, never filenames or user
   text, so they do not require JSON string escaping. */
void recordStart(FILE *file, size_t id, const HopfieldContext *ctx,
                 const char *rule, int noise, const char *corruption,
                 int trial, int pattern, bool noisyReference,
                 const double reference[], const double input[],
                 double trainingSeconds)
{
   if (!file)
      return;
   fprintf(file, "{\"type\":\"run\",\"id\":%zu,\"rule\":\"%s\","
                 "\"stored_patterns\":%d,\"noise_percent\":%d,"
                 "\"corruption\":\"%s\",\"trial\":%d,\"pattern\":%d,"
                 "\"reference\":\"%s\",\"training_seconds\":",
           id, rule, ctx->nPatterns, noise, corruption, trial, pattern,
           noisyReference ? "noisy" : "stored");
   number(file, trainingSeconds);
   fputs(",\"original\":", file);
   raster(file, reference, ctx->patternSize);
   fputs(",\"input\":", file);
   raster(file, input, ctx->patternSize);
   fputs("}\n", file);
}

void recordIteration(FILE *file, size_t id, int iteration, double energy,
                     const double pattern[], int size)
{
   if (!file)
      return;
   fprintf(file, "{\"type\":\"step\",\"id\":%zu,\"iteration\":%d,"
                 "\"energy\":", id, iteration);
   number(file, energy);
   fputs(",\"pixels\":", file);
   raster(file, pattern, size);
   fputs("}\n", file);
}

void recordResult(FILE *file, size_t id, const double output[], int size,
                  bool converged, double energy, double overlap,
                  int hamming, double recallSeconds,
                  const RecallAnalysis *analysis)
{
   if (!file)
      return;
   fprintf(file, "{\"type\":\"result\",\"id\":%zu,\"converged\":%s,"
                 "\"hamming\":%d,\"attractor\":\"%s\","
                 "\"matched_pattern\":%d,\"overlap\":",
           id, converged ? "true" : "false", hamming, analysis->kind,
           analysis->matchedPattern);
   number(file, overlap);
   fputs(",\"energy\":", file);
   number(file, energy);
   fputs(",\"recall_seconds\":", file);
   number(file, recallSeconds);
   fputs(",\"output\":", file);
   raster(file, output, size);
   fputs(",\"closest\":[", file);
   for (int i = 0; i < analysis->count; i++) {
      fprintf(file, "%s{\"pattern\":%d,\"overlap\":", i ? "," : "",
              analysis->indices[i]);
      number(file, analysis->overlaps[i]);
      fputc('}', file);
   }
   fputs("]}\n", file);
}

void recordFailure(FILE *file, const char *rule, int stored, int noise,
                   int trial, const char *corruption)
{
   if (file)
      fprintf(file, "{\"type\":\"failure\",\"rule\":\"%s\","
                    "\"stored_patterns\":%d,\"noise_percent\":%d,"
                    "\"trial\":%d,\"corruption\":\"%s\","
                    "\"error\":\"training_failed\"}\n",
              rule, stored, noise, trial, corruption);
}

void recordEnd(FILE *file, int exitCode)
{
   if (file)
      fprintf(file, "{\"type\":\"end\",\"exit_code\":%d}\n", exitCode);
}
