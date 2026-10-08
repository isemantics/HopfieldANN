#ifndef HOPFIELDCALC_H
#define HOPFIELDCALC_H

#include "HopfieldContext.h"
#include "HopfieldUtil.h"
#include <stdbool.h>

typedef void (*ConvergenceCallback)(int iteration, double energy,
                                    const double pattern[],
                                    void *user_data);

#ifdef __cplusplus
extern "C" {
#endif

/* Called only for changed neurons; sweep and neuron indices are 1-based.
   Observers must not change network state or consume random numbers. */
typedef void (*NeuronCallback)(int sweep, int neuron, double value,
                               void *user_data);
bool convergePatternTraced(HopfieldContext *ctx, const double input[],
                           double output[], ConvergenceCallback callback,
                           NeuronCallback neuronCallback, void *user_data,
                           double *finalEnergy);

/* Pure functions - no context needed */
bool isSymmetric(const int patternSize, const double *const w[]);
bool hasZeroDiagonal(const int patternSize, const double *const w[]);
int storageCapacity(const int patternSize);

double calcOverlap(const int patternSize, const double pattern1[],
                   const double pattern2[]);

int calcHammingDistance(const int patternSize, const double pattern1[],
                        const double pattern2[]);

/* Context-dependent functions */
bool learnHebbian(HopfieldContext *ctx);
bool learnStorkey(HopfieldContext *ctx);
bool learnPseudoInverse(HopfieldContext *ctx);
bool learnDaydreaming(HopfieldContext *ctx);
bool learnModernHopfield(HopfieldContext *ctx);
int addNoiseToPattern(HopfieldContext *ctx, const int patNumber,
                      int chance);

void calcOutputPattern(const int patternSize, const double *const w[],
                       const double inputPattern[],
                       double outputPattern[]);

void calcOutputPatternAsync(const int patternSize, const double *const w[],
                            double pattern[]);

double calcEnergy(const int patternSize, const double pattern[],
                  const double *const w[]);

/* Modern Hopfield energy E(x) = -lse(beta, Xi^T x) + 0.5 * ||x||^2. Uses
   the stored patterns as the memory (requires ctx->modernHopfield). */
double calcModernEnergy(const HopfieldContext *ctx,
                        const double pattern[]);

bool convergePattern(HopfieldContext *ctx, const double inputPattern[],
                     double outputPattern[], ConvergenceCallback callback,
                     void *user_data, double *finalEnergy);

bool calcAssociatedPattern(HopfieldContext *ctx,
                           const double inputPattern[],
                           double associatedPattern[],
                           double *finalEnergy);

#ifdef __cplusplus
}
#endif

#endif
