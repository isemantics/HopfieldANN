#include "cli.h"
#include "AppInfo.h"
#include "HopfieldCalc.h"
#include "HopfieldAnalysis.h"
#include "HopfieldRecord.h"
#include "HopfieldContext.h"
#include "HopfieldIO.h"
#include "HopfieldNoise.h"
#include "HopfieldUtil.h"

#include <errno.h>
#include <getopt.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#define MAXFILENAME_SIZE 100
#define MAX_CONFIG_LINE 256

typedef enum {
   RULE_HEBBIAN,
   RULE_STORKEY,
   RULE_PSEUDO_INVERSE,
   RULE_DAYDREAMING,
   RULE_MODERN
} LearningRule;

/* Scratch buffers and recall parameters carried across the interactive
   session. Owned by run_cli and released in its cleanup path. */
typedef struct {
   double *inputPattern;
   double *inputPatternWithNoise;
   double *outputPattern;
   int indexPattern;
   int noise;
} SimState;

typedef struct {
   double count, exact, converged, overlap, hamming;
} SweepTotals;

/* Batch mode configuration parsed from CLI and config file. */
typedef struct {
   LearningRule rule;
   bool ruleSet;
   int *patterns;
   int nPatterns;
   int noise;
   unsigned int seed;
   bool seedSet;
   bool quiet;
   bool verbose;
   char *outputFile;
   char *saveWeightsFile;
   char *loadWeightsFile;
   bool helpRequested;
   bool batchMode;
   const char *csvFile;
   FILE *csv;
   const char *recordFile;
   FILE *record;
   size_t nextRun;
   double trainingSeconds;
   bool compare;
   bool sweep;
   bool capacity;
   HopfieldNoise corruption;
   bool corruptionSet;
   int sweepStart, sweepEnd, sweepStep;
   int trials, trial;
   SweepTotals sweepTotals[RULE_MODERN + 1];
   double **comparisonInputs;
   double totalOverlap;
   double totalHamming;
   double totalIterations;
   double totalRecallSeconds;
   int exactMatches;
   int convergedCount;
} BatchConfig;

/* Settings loaded from config file. */
typedef struct {
   LearningRule rule;
   bool ruleSet;
   unsigned int seed;
   bool seedSet;
   int noise;
   bool noiseSet;
   bool verbose;
   char *saveWeightsFile;
   char *loadWeightsFile;
   char *outputFile;
} ConfigFileSettings;

/* Detect aliases too, so CSV export cannot truncate an input or another
   requested output. */
static bool same_file(const char *a, const char *b)
{
   if (a == NULL || b == NULL)
      return false;
   struct stat sa, sb;
   return strcmp(a, b) == 0 ||
          (stat(a, &sa) == 0 && stat(b, &sb) == 0 &&
           sa.st_dev == sb.st_dev && sa.st_ino == sb.st_ino);
}

static const char *rule_key(LearningRule rule)
{
   switch (rule) {
      case RULE_STORKEY: return "storkey";
      case RULE_PSEUDO_INVERSE: return "pseudo-inverse";
      case RULE_DAYDREAMING: return "daydreaming";
      case RULE_MODERN: return "modern";
      default: return "hebbian";
   }
}

static const char *noise_key(HopfieldNoise noise)
{
   switch (noise) {
      case HOPFIELD_NOISE_ERASE: return "erase";
      case HOPFIELD_NOISE_BLOCK: return "block";
      case HOPFIELD_NOISE_LEFT: return "left";
      case HOPFIELD_NOISE_RIGHT: return "right";
      case HOPFIELD_NOISE_TOP: return "top";
      case HOPFIELD_NOISE_BOTTOM: return "bottom";
      default: return "flip";
   }
}

static bool parse_seed(const char *text, unsigned int *seed)
{
   if (*text < '0' || *text > '9')
      return false;
   errno = 0;
   char *end;
   unsigned long value = strtoul(text, &end, 10);
   if (errno == ERANGE || *end != '\0' || value > UINT_MAX)
      return false;
   *seed = (unsigned int)value;
   return true;
}

static bool usage(int argc);
static void clearInput(void);
static void handle_error(HopfieldError err);
static bool learnPatterns(HopfieldContext *ctx, LearningRule rule);
static const char *ruleNameOf(LearningRule rule);
static bool ensurePatternBuffers(HopfieldContext *ctx,
                                 double **inputPattern,
                                 double **inputPatternWithNoise,
                                 double **outputPattern);
static void print_neurons(const HopfieldContext *ctx);
static void prompt_learning_rule(LearningRule *rule);
static bool prepare_network(HopfieldContext *ctx, LearningRule rule,
                            SimState *state, bool quiet);
static bool read_and_prepare(HopfieldContext *ctx, const char fileName[],
                             LearningRule rule, SimState *state);
static bool prompt_reload(HopfieldContext *ctx, LearningRule rule,
                          SimState *state);
static bool run_simulation(HopfieldContext *ctx, int argc, bool repeat,
                           SimState *state);

/* New functions for batch mode */
static bool parse_cli_options(int argc, char *argv[], BatchConfig *cfg);
static void free_batch_config(BatchConfig *cfg);
static bool read_config_file(ConfigFileSettings *settings);
static bool apply_config_to_batch(const ConfigFileSettings *config,
                                  BatchConfig *cfg);
static int run_batch_mode(HopfieldContext *ctx, BatchConfig *cfg,
                          SimState *state, bool mode2);
static int run_capacity(HopfieldContext *ctx, BatchConfig *cfg,
                        SimState *state);
static int run_sweep(HopfieldContext *ctx, BatchConfig *cfg,
                     SimState *state);
static int run_comparison(HopfieldContext *ctx, BatchConfig *cfg,
                          SimState *state, bool mode2);
static int run_single_pattern(HopfieldContext *ctx, int selection,
                              int patternIndex, int noisePercent,
                              BatchConfig *cfg,
                              SimState *state, bool *converged,
                              bool firstPattern, bool mode2);
struct VerboseCallbackData {
   const HopfieldContext *ctx;
   const double *inputPattern;
   int iterations;
   bool verbose;
   FILE *record;
   size_t runId;
};

static void verbose_iteration_callback(int iteration, double energy,
                                       const double pattern[],
                                       void *user_data)
{
   struct VerboseCallbackData *data = user_data;
   if (data == NULL)
      return;
   data->iterations = iteration;
   recordIteration(data->record, data->runId, iteration, energy,
                   pattern, data->ctx->patternSize);
   if (!data->verbose)
      return;
   showPatternAndDifference(data->ctx, data->inputPattern, pattern);
   printf("\n    Energy = %9.4f\n\n", energy);
}

static bool save_recall_output(const HopfieldContext *ctx,
                               const double *pattern, const char *filename,
                               bool firstPattern);
static void print_batch_result(int patternIdx, double overlap, int hamming,
                               int patternSize, bool converged);
static void print_usage(void);

int run_cli(HopfieldContext *ctx, int argc, char *argv[])
{
   /* Parse CLI options into BatchConfig */
   BatchConfig cfg = {0};
   if (!parse_cli_options(argc, argv, &cfg)) {
      if (cfg.helpRequested) {
         print_usage();
      }
      free_batch_config(&cfg);
      return 1;
   }

   if (cfg.helpRequested) {
      print_usage();
      free_batch_config(&cfg);
      return 0;
   }

   if (cfg.compare && cfg.ruleSet) {
      fprintf(stderr, "Error: --compare cannot be combined with --rule\n");
      free_batch_config(&cfg);
      return 1;
   }
   if (cfg.csvFile && !cfg.batchMode) {
      fprintf(stderr, "Error: --csv requires --pattern\n");
      free_batch_config(&cfg);
      return 1;
   }

   /* Load config file (CLI overrides config) */
   ConfigFileSettings config = {0};
   if (!read_config_file(&config)) {
      free(config.saveWeightsFile);
      free(config.loadWeightsFile);
      free(config.outputFile);
      free_batch_config(&cfg);
      return 1;
   }
   apply_config_to_batch(&config, &cfg);

   if ((cfg.compare || cfg.sweep || cfg.capacity) && (cfg.loadWeightsFile || cfg.saveWeightsFile ||
                       cfg.outputFile)) {
      fprintf(stderr, "Error: --compare/--sweep cannot use weight or pattern "
                      "output files; use --csv for results\n");
      free_batch_config(&cfg);
      return 1;
   }

   /* Seed once for the complete session, including stochastic training. */
   if (!cfg.seedSet)
      cfg.seed = (unsigned int)time(NULL);
   srand(cfg.seed);
   if (!cfg.quiet)
      printf("- Random seed: %u\n", cfg.seed);

   /* Validate positional arguments */
   if (argc - optind < 1 || argc - optind > 2) {
      fprintf(stderr,
              "\n\tUSAGE: hopfieldann <input patterns filename> "
              "[noisy patterns filename] [options]\n\n");
      free_batch_config(&cfg);
      return 1;
   }

   bool mode2 = (argc - optind == 2);

   if (cfg.corruptionSet && (!cfg.batchMode || mode2)) {
      fprintf(stderr, "Error: --corruption requires batch mode "
                      "with clean stored patterns\n");
      free_batch_config(&cfg);
      return 1;
   }
   if ((cfg.sweep || cfg.capacity) && mode2) {
      fprintf(stderr, "Error: --sweep/--capacity requires clean stored patterns\n");
      free_batch_config(&cfg);
      return 1;
   }

   /* Validate mode 1 requirements (after config applied) */
   if (cfg.batchMode && cfg.noise < 0 && !mode2 && !cfg.sweep) {
      fprintf(stderr, "\n\tERROR: --noise required with --pattern\n\n");
      free_batch_config(&cfg);
      return 1;
   }

   const char *patternFile = argv[optind];
   const char *noisyFile = (argc - optind == 2) ? argv[optind + 1] : NULL;

   if (same_file(cfg.csvFile, patternFile) ||
       same_file(cfg.csvFile, noisyFile) ||
       same_file(cfg.csvFile, cfg.outputFile) ||
       same_file(cfg.csvFile, cfg.saveWeightsFile) ||
       same_file(cfg.csvFile, cfg.loadWeightsFile)) {
      fprintf(stderr, "Error: CSV must use a separate file\n");
      free_batch_config(&cfg);
      return 1;
   }

   if (cfg.corruptionSet && !cfg.quiet)
      printf("- Corruption: %s (? = unknown pixel)\n",
             noise_key(cfg.corruption));

   if (cfg.recordFile && (!cfg.batchMode || cfg.loadWeightsFile ||
       same_file(cfg.recordFile, patternFile) ||
       same_file(cfg.recordFile, noisyFile) ||
       same_file(cfg.recordFile, cfg.csvFile) ||
       same_file(cfg.recordFile, cfg.outputFile) ||
       same_file(cfg.recordFile, cfg.saveWeightsFile))) {
      fprintf(stderr, "Error: --record requires batch training and a "
                      "separate output file\n");
      free_batch_config(&cfg);
      return 1;
   }

   /* Load pattern file */
   if (!cfg.quiet) {
      printf("Hopfield's ANN associative memory: " APPNAME_VERSION
             "\n\n- Input patterns file name: %s, loading .... ",
             patternFile);
   }

   HopfieldError err = readFile(ctx, patternFile);
   if (err != HOPFIELD_OK) {
      handle_error(err);
      free_batch_config(&cfg);
      return 1;
   }
   if (!cfg.quiet) {
      printf("ready\n\n");
      print_neurons(ctx);
   }

   /* Load noisy file if provided */
   if (noisyFile) {
      if (!cfg.quiet) {
         printf("- Noisy patterns file name: %s\n\n", noisyFile);
         printf("- Loading noisy patterns data .... ");
      }
      err = readNoisyFile(ctx, noisyFile);
      if (err != HOPFIELD_OK) {
         handle_error(err);
         free_batch_config(&cfg);
         return 1;
      }
      if (!cfg.quiet) {
         printf("ready\n\n");
         printf("- Number of noisy patterns: %d\n\n", ctx->nNoisyPatterns);
      }
   }

   SimState state = {0};
   int result = 0;

   if (cfg.recordFile) {
      cfg.record = fopen(cfg.recordFile, "w");
      if (!cfg.record) {
         fprintf(stderr, "Error: Cannot open recording file\n");
         result = 1;
         goto cleanup;
      }
      recordSession(cfg.record, ctx, cfg.seed);
   }

   /* Batch mode or interactive */
   if (cfg.capacity) {
      result = run_capacity(ctx, &cfg, &state);
   }
   else if (cfg.sweep) {
      result = run_sweep(ctx, &cfg, &state);
   }
   else if (cfg.compare) {
      result = run_comparison(ctx, &cfg, &state, mode2);
   }
   else if (cfg.batchMode) {
      result = run_batch_mode(ctx, &cfg, &state, mode2);
   }
   else {
      /* Determine learning rule: CLI > config > prompt */
      LearningRule rule = RULE_HEBBIAN;
      bool ruleSet = false;

      if (cfg.ruleSet) {
         rule = cfg.rule;
         ruleSet = true;
      }
      else if (config.ruleSet) {
         rule = config.rule;
         ruleSet = true;
      }

      if (!ruleSet) {
         prompt_learning_rule(&rule);
      }

      if (!prepare_network(ctx, rule, &state, cfg.quiet)) {
         result = 1;
         goto cleanup;
      }

      /* Save weights if requested */
      if (cfg.saveWeightsFile) {
         if (!cfg.quiet) {
            printf("- Saving weight matrix to %s .... ",
                   cfg.saveWeightsFile);
         }
         if (!hopfield_save_weights(ctx, cfg.saveWeightsFile)) {
            fprintf(stderr, "\n\tERROR: Failed to save weights\n\n");
            result = 1;
            goto cleanup;
         }
         if (!cfg.quiet) {
            printf("ready\n\n");
         }
      }

      if (argc - optind == 2) {
         /* Mode 2: noisy file provided, interactive noisy pattern
          * selection */
         int menu = 0;
         bool firstRun = true;
         const char menuChars[] = "ENenRr";

         while (menu != 'E' && menu != 'e') {
            bool enterPressed = (menu == '\n');
            if (enterPressed) {
               menu = 'R';
            }

            if (firstRun || strchr(menuChars, menu) != NULL) {
               firstRun = false;
               bool repeat = (menu == 'R' || menu == 'r');
               if (!run_simulation(ctx, 3, repeat, &state)) {
                  result = 1;
                  goto cleanup;
               }
            }
            if (!cfg.quiet) {
               printf(
                  "- E(xit), N(ext simulation), R(un again), "
                  "<Enter>(repeat) .... ");
            }
            if (!enterPressed) {
               clearInput();
            }
            menu = getchar();
         }
      }
      else {
         /* Mode 1: interactive pattern/noise selection */
         int menu = 0;
         bool firstRun = true;
         const char menuChars[] = "ELNelnRr";

         while (menu != 'E' && menu != 'e') {
            bool enterPressed = (menu == '\n');
            if (enterPressed) {
               menu = 'R';
            }
            if (menu == 'L' || menu == 'l') {
               if (!prompt_reload(ctx, rule, &state)) {
                  result = 1;
                  goto cleanup;
               }
            }

            if (firstRun || strchr(menuChars, menu) != NULL) {
               firstRun = false;
               bool repeat = (menu == 'R' || menu == 'r');
               if (!run_simulation(ctx, 2, repeat, &state)) {
                  result = 1;
                  goto cleanup;
               }
            }
            if (!cfg.quiet) {
               printf(
                  "- E(xit), L(oad new patterns data file), N(ext "
                  "simulation),\n"
                  "  R(un again), <Enter>(repeat) .... ");
            }
            if (!enterPressed) {
               clearInput();
            }
            menu = getchar();
         }
      }
   }

cleanup:
   if (cfg.record) {
      if (ferror(cfg.record))
         result = 1;
      recordEnd(cfg.record, result);
      bool failed = ferror(cfg.record) != 0;
      if (fclose(cfg.record) != 0)
         failed = true;
      cfg.record = NULL;
      if (failed) {
         fprintf(stderr, "Error: Failed to write recording\n");
         result = 1;
      }
   }
   free(state.inputPattern);
   free(state.inputPatternWithNoise);
   free(state.outputPattern);
   free_batch_config(&cfg);
   return result;
}

/* Print the network dimensions derived from the loaded pattern file. */
static void print_neurons(const HopfieldContext *ctx)
{
   printf("- Number of neurons: %d * %d = %d, number of patterns = %d\n",
          ctx->nRows, ctx->nColumns, ctx->patternSize, ctx->nPatterns);
}

/* Ask for the learning rule and update *rule. A blank answer selects the
   default (Hebbian). */
static void prompt_learning_rule(LearningRule *rule)
{
   printf(
      "- Learning rule: (H)ebbian, (S)torkey, (P)seudo-inverse, "
      "(D)aydreaming or (M)odern? [H]: ");
   int ruleChoice = getchar();
   if (ruleChoice == EOF) {
      printf("\n- No input, using Hebbian learning rule\n");
   }
   else if (ruleChoice == 'S' || ruleChoice == 's') {
      *rule = RULE_STORKEY;
   }
   else if (ruleChoice == 'P' || ruleChoice == 'p') {
      *rule = RULE_PSEUDO_INVERSE;
   }
   else if (ruleChoice == 'D' || ruleChoice == 'd') {
      *rule = RULE_DAYDREAMING;
   }
   else if (ruleChoice == 'M' || ruleChoice == 'm') {
      *rule = RULE_MODERN;
   }
   if (ruleChoice != '\n' && ruleChoice != EOF) {
      clearInput();
   }
   puts("");
}

/* (Re)allocate the session buffers, warn about the storage-capacity limit,
   then learn the loaded patterns with the given rule. Common to startup
   and file reload. */
static bool prepare_network(HopfieldContext *ctx, LearningRule rule,
                            SimState *state, bool quiet)
{
   if (!ensurePatternBuffers(ctx, &state->inputPattern,
                             &state->inputPatternWithNoise,
                             &state->outputPattern)) {
      fprintf(stderr, "Error: Out of memory\n");
      return false;
   }

   int stCapacity = storageCapacity(ctx->patternSize);
   if (stCapacity < ctx->nPatterns) {
      fprintf(stderr,
              "- Warning: associative storage capacity %d exceeded\n",
              stCapacity);
   }

   if (!quiet) {
      printf(
         "- Learning patterns by %s learning rule, training starts .... ",
         ruleNameOf(rule));
   }
   if (!learnPatterns(ctx, rule)) {
      fprintf(stderr, "Error: Failed to learn patterns\n");
      return false;
   }
   if (!quiet) {
      printf("ready\n");
      printf("- Learning result: 1 connection matrix, size %d x %d\n\n",
             ctx->nRows * ctx->nColumns, ctx->nRows * ctx->nColumns);
   }
   return true;
}

/* Load a pattern file, report its dimensions, then learn (shared by the
   startup path's reload and the interactive L command). */
static bool read_and_prepare(HopfieldContext *ctx, const char fileName[],
                             LearningRule rule, SimState *state)
{
   printf("\n- Loading input patterns data .... ");
   HopfieldError err = readFile(ctx, fileName);
   if (err != HOPFIELD_OK) {
      handle_error(err);
      return false;
   }
   printf("ready\n\n");
   print_neurons(ctx);
   return prepare_network(ctx, rule, state,
                          false); /* interactive mode: not quiet */
}

/* Interactive file reload: read a file name (with overlong-name
   detection), load it and learn with the current rule. */
static bool prompt_reload(HopfieldContext *ctx, LearningRule rule,
                          SimState *state)
{
   char fileName[MAXFILENAME_SIZE] = {0};
   clearInput();
   printf("- Input patterns file name: ");
   if (fgets(fileName, MAXFILENAME_SIZE, stdin) == NULL) {
      fprintf(stderr, "\n\tERROR: could not read file name\n\n");
      return false;
   }
   size_t fileNameLen = strlen(fileName);
   if (fileNameLen > 0 && fileName[fileNameLen - 1] == '\n') {
      fileName[fileNameLen - 1] = '\0';
   }
   else if (fileNameLen == MAXFILENAME_SIZE - 1) {
      /* Buffer filled without a newline: the name is either exactly
         MAXFILENAME_SIZE - 1 characters (newline still pending) or
         longer. Peek at the next character to tell the two apart. */
      int c = getchar();
      if (c != '\n' && c != EOF) {
         clearInput();
         fprintf(stderr,
                 "\n\tERROR: file name too long "
                 "(max %d characters)\n\n",
                 MAXFILENAME_SIZE - 1);
         return false;
      }
   }
   return read_and_prepare(ctx, fileName, rule, state);
}

/* Run one recall simulation. In mode 1 (argc == 2) the stored pattern is
   corrupted on the fly; in mode 2 (argc == 3) a pre-made noisy pattern is
   recalled. On repeat the previous index/noise parameters are reused. */
static bool run_simulation(HopfieldContext *ctx, int argc, bool repeat,
                           SimState *state)
{
   if (argc == 2) {
      if (!repeat) {
         printf("- Choose pattern to disturb by noise, index (1..%d): ",
                ctx->nPatterns);
         if (scanf(" %d", &state->indexPattern) != 1) {
            fprintf(stderr, "\n\tERROR: invalid input\n\n");
            return false;
         }
         puts("");
         if (state->indexPattern < 1 ||
             state->indexPattern > ctx->nPatterns) {
            fprintf(stderr, "\n\tERROR: index %d out of range\n\n",
                    state->indexPattern);
            return false;
         }
         state->indexPattern--;
         showIndexedPattern(ctx, state->indexPattern);
         puts("");
         copyPattern(ctx->patternSize, ctx->patterns[state->indexPattern],
                     state->inputPattern);
         copyPattern(ctx->patternSize, ctx->patterns[state->indexPattern],
                     state->inputPatternWithNoise);
         printf("- Pattern %d noise level [%%]: ",
                state->indexPattern + 1);
         if (scanf(" %d", &state->noise) != 1) {
            fprintf(stderr, "\n\tERROR: invalid input\n\n");
            return false;
         }
         if (state->noise < 0 || state->noise > MAX_NOISE_PERCENT) {
            fprintf(stderr,
                    "\n\tERROR: noise level %d out of range (0..%d)\n\n",
                    state->noise, MAX_NOISE_PERCENT);
            return false;
         }
         if (state->noise > MAX_INFORMATIVE_NOISE_PERCENT) {
            printf(
               "- Note: above %d%% noise the input is anti-correlated "
               "with the stored pattern; the network converges to the "
               "inverted pattern.\n",
               MAX_INFORMATIVE_NOISE_PERCENT);
         }
      }

      addNoiseToPattern(ctx, state->indexPattern, state->noise);
      copyPattern(ctx->patternSize,
                  ctx->noisyPatterns[state->indexPattern],
                  state->inputPatternWithNoise);
      printf("\n\n- Pattern %d as 2D image and %d%% noisy pixels:\n\n",
             state->indexPattern + 1, state->noise);
      showAssociatedPattern(ctx, state->inputPattern,
                            state->inputPatternWithNoise,
                            state->outputPattern);
      puts("");
      return true;
   }

   if (argc == 3) {
      if (!repeat) {
         printf("\n- Choose noisy pattern, index (1..%d): ",
                ctx->nNoisyPatterns);
         if (scanf(" %d", &state->indexPattern) != 1) {
            fprintf(stderr, "\n\tERROR: invalid input\n\n");
            return false;
         }
         puts("");
         if (state->indexPattern < 1 ||
             state->indexPattern > ctx->nNoisyPatterns) {
            fprintf(stderr, "\n\tERROR: index %d out of range\n\n",
                    state->indexPattern);
            return false;
         }
         state->indexPattern--;
         puts("");
         showIndexedNoisyPattern(ctx, state->indexPattern);
         puts("");
         copyPattern(ctx->patternSize,
                     ctx->noisyPatterns[state->indexPattern],
                     state->inputPattern);
      }
      printf("\n\n- Pattern %d as 2D image:\n\n", state->indexPattern + 1);
      showAssociatedPattern(ctx, state->inputPattern, state->inputPattern,
                            state->outputPattern);
      puts("");
      return true;
   }

   fprintf(stderr, "\n\tSYSTEM ERROR: this should never happen!\n\n");
   return false;
}

static bool usage(int argc)
{
   if ((argc == 2) || (argc == 3)) {
      return true;
   }
   fprintf(stderr, "\n\tUSAGE: hopfieldann <input patterns filename>\n");
   fprintf(stderr,
           "\n\tUSAGE: hopfieldann <input patterns filename> "
           "<noisy patterns filename>\n\n");
   return false;
}

static void clearInput(void)
{
   int c;
   while ((c = getchar()) != '\n' && c != EOF) {
   }
}

static const char *ruleNameOf(LearningRule rule)
{
   switch (rule) {
      case RULE_STORKEY:
         return "Storkey";
      case RULE_PSEUDO_INVERSE:
         return "pseudo-inverse";
      case RULE_DAYDREAMING:
         return "Daydreaming";
      case RULE_MODERN:
         return "modern Hopfield (softmax)";
      default:
         return "Hebbian";
   }
}

static bool learnPatterns(HopfieldContext *ctx, LearningRule rule)
{
   switch (rule) {
      case RULE_STORKEY:
         return learnStorkey(ctx);
      case RULE_PSEUDO_INVERSE:
         return learnPseudoInverse(ctx);
      case RULE_DAYDREAMING:
         return learnDaydreaming(ctx);
      case RULE_MODERN:
         return learnModernHopfield(ctx);
      default:
         return learnHebbian(ctx);
   }
}

static bool ensurePatternBuffers(HopfieldContext *ctx,
                                 double **inputPattern,
                                 double **inputPatternWithNoise,
                                 double **outputPattern)
{
   if (ctx == NULL || ctx->patternSize <= 0) {
      return false;
   }
   size_t bytes = (size_t)ctx->patternSize * sizeof(double);
   double *ip = (double *)malloc(bytes);
   if (ip == NULL) {
      return false;
   }
   double *np = (double *)malloc(bytes);
   if (np == NULL) {
      free(ip);
      return false;
   }
   double *op = (double *)malloc(bytes);
   if (op == NULL) {
      free(ip);
      free(np);
      return false;
   }
   free(*inputPattern);
   free(*inputPatternWithNoise);
   free(*outputPattern);
   *inputPattern = ip;
   *inputPatternWithNoise = np;
   *outputPattern = op;
   return true;
}

static void handle_error(HopfieldError err)
{
   switch (err) {
      case HOPFIELD_ERR_FILE_NOT_FOUND:
         fprintf(stderr, "Error: File not found\n");
         break;
      case HOPFIELD_ERR_INVALID_FORMAT:
         fprintf(stderr, "Error: Invalid file format\n");
         break;
      case HOPFIELD_ERR_INDEX_OUT_OF_RANGE:
         fprintf(stderr, "Error: Index out of range\n");
         break;
      case HOPFIELD_ERR_SIZE_EXCEEDED:
         fprintf(stderr, "Error: Pattern size exceeded maximum\n");
         break;
      case HOPFIELD_ERR_OUT_OF_MEMORY:
         fprintf(stderr, "Error: Out of memory\n");
         break;
      default:
         fprintf(stderr, "Error: Unknown error (%d)\n", err);
   }
}

/* Parse a comma-separated list of integers. Returns allocated array and
 * count. */
static int *parse_int_list(const char *str, int *count)
{
   *count = 0;
   if (str == NULL || *str == '\0')
      return NULL;

   size_t n = 1;
   for (const char *p = str; *p; p++) {
      if (*p == ',')
         n++;
   }
   if (n > INT_MAX || n > SIZE_MAX / sizeof(int))
      return NULL;
   int *arr = malloc(n * sizeof(int));
   if (arr == NULL)
      return NULL;

   const char *p = str;
   for (size_t i = 0; i < n; i++) {
      char *end;
      errno = 0;
      long value = strtol(p, &end, 10);
      if (*p < '0' || *p > '9' || errno == ERANGE ||
          (*end != ',' && *end != '\0') || value < 1 ||
          value > INT_MAX) {
         free(arr);
         return NULL;
      }
      arr[i] = (int)value - 1;
      p = *end == ',' ? end + 1 : end;
   }
   *count = (int)n;
   return arr;
}

/* Parse CLI options using getopt_long. */
static bool parse_cli_options(int argc, char *argv[], BatchConfig *cfg)
{
   memset(cfg, 0, sizeof(BatchConfig));
   cfg->noise = -1;
   cfg->trial = 1;
   cfg->rule = RULE_HEBBIAN;

   static struct option long_options[] = {
      {"record", required_argument, 0, 261},
      {"capacity", no_argument, 0, 260},
      {"corruption", required_argument, 0, 259},
      {"sweep", required_argument, 0, 257},
      {"trials", required_argument, 0, 258},
      {"compare", no_argument, 0, 256},
      {"csv", required_argument, 0, 'c'},
      {"rule", required_argument, 0, 'r'},
      {"pattern", required_argument, 0, 'p'},
      {"noise", required_argument, 0, 'n'},
      {"seed", required_argument, 0, 's'},
      {"quiet", no_argument, 0, 'q'},
      {"verbose", no_argument, 0, 'v'},
      {"help", no_argument, 0, 'h'},
      {"output", required_argument, 0, 'o'},
      {"save-weights", required_argument, 0, 'w'},
      {"load-weights", required_argument, 0, 'l'},
      {0, 0, 0, 0}};

   int opt;
   while ((opt = getopt_long(argc, argv, "r:p:n:s:qvho:w:l:c:", long_options,
                             NULL)) != -1) {
      switch (opt) {
         case 261:
            cfg->recordFile = optarg;
            break;
         case 260:
            cfg->capacity = true;
            break;
         case 259: {
            bool found = false;
            for (HopfieldNoise noise = HOPFIELD_NOISE_FLIP;
                 noise <= HOPFIELD_NOISE_BOTTOM; noise++) {
               if (strcmp(optarg, noise_key(noise)) == 0) {
                  cfg->corruption = noise;
                  found = true;
                  break;
               }
            }
            if (!found) {
               fprintf(stderr, "Error: unknown corruption '%s'\n", optarg);
               return false;
            }
            cfg->corruptionSet = true;
            break;
         }
         case 257: {
            const char *p = optarg;
            int values[3];
            for (int i = 0; i < 3; i++) {
               char *end;
               errno = 0;
               long value = strtol(p, &end, 10);
               if (*p < '0' || *p > '9' || errno == ERANGE ||
                   value > 100 || (i < 2 ? *end != ':' : *end != '\0')) {
                  fprintf(stderr, "Error: invalid sweep; use START:END:STEP\n");
                  return false;
               }
               values[i] = (int)value;
               p = i < 2 ? end + 1 : end;
            }
            if (values[0] > values[1] || values[2] == 0) {
               fprintf(stderr, "Error: invalid sweep range or step\n");
               return false;
            }
            cfg->sweep = true;
            cfg->sweepStart = values[0];
            cfg->sweepEnd = values[1];
            cfg->sweepStep = values[2];
            break;
         }
         case 258: {
            unsigned int value;
            if (!parse_seed(optarg, &value) || value == 0 || value > INT_MAX) {
               fprintf(stderr, "Error: invalid trials count\n");
               return false;
            }
            cfg->trials = (int)value;
            break;
         }
         case 256:
            cfg->compare = true;
            break;
         case 'c':
            cfg->csvFile = optarg;
            break;
         case 'r': {
            if (strcasecmp(optarg, "hebbian") == 0)
               cfg->rule = RULE_HEBBIAN;
            else if (strcasecmp(optarg, "storkey") == 0)
               cfg->rule = RULE_STORKEY;
            else if (strcasecmp(optarg, "pseudo-inverse") == 0)
               cfg->rule = RULE_PSEUDO_INVERSE;
            else if (strcasecmp(optarg, "daydreaming") == 0)
               cfg->rule = RULE_DAYDREAMING;
            else if (strcasecmp(optarg, "modern") == 0)
               cfg->rule = RULE_MODERN;
            else {
               fprintf(stderr, "\n\tERROR: unknown learning rule '%s'\n\n",
                       optarg);
               return false;
            }
            cfg->ruleSet = true;
            break;
         }
         case 'p': {
            free(cfg->patterns);
            cfg->patterns = parse_int_list(optarg, &cfg->nPatterns);
            if (cfg->patterns == NULL && cfg->nPatterns == 0) {
               fprintf(stderr, "\n\tERROR: invalid pattern list '%s'\n\n",
                       optarg);
               return false;
            }
            break;
         }
         case 'n': {
            char *endptr;
            long val = strtol(optarg, &endptr, 10);
            if (endptr == optarg || *endptr != '\0' || val < 0 ||
                val > MAX_NOISE_PERCENT) {
               fprintf(
                  stderr,
                  "\n\tERROR: noise level %s out of range (0..%d)\n\n",
                  optarg, MAX_NOISE_PERCENT);
               return false;
            }
            cfg->noise = (int)val;
            break;
         }
         case 's': {
            if (!parse_seed(optarg, &cfg->seed)) {
               fprintf(stderr, "Error: invalid seed value '%s'\n", optarg);
               return false;
            }
            cfg->seedSet = true;
            break;
         }
         case 'q':
            cfg->quiet = true;
            break;
         case 'v':
            cfg->verbose = true;
            break;
         case 'h':
            cfg->helpRequested = true;
            break;
         case 'o':
            cfg->outputFile = strdup(optarg);
            break;
         case 'w':
            cfg->saveWeightsFile = strdup(optarg);
            break;
         case 'l':
            cfg->loadWeightsFile = strdup(optarg);
            break;
         case '?':
            return false;
         default:
            return false;
      }
   }

   /* Batch mode if pattern(s) specified (mode 1) or pattern specified
    * (mode 2) */
   if ((cfg->trials && !cfg->sweep && !cfg->capacity) || (cfg->sweep && cfg->noise >= 0)) {
      fprintf(stderr, "Error: --trials requires --sweep or --capacity; "
                      "--sweep replaces --noise\n");
      return false;
   }
   if (cfg->capacity && (cfg->sweep || cfg->nPatterns > 0)) {
      fprintf(stderr, "Error: --capacity cannot use --sweep or --pattern\n");
      return false;
   }
   if (!cfg->trials)
      cfg->trials = 10;
   cfg->batchMode = cfg->capacity || cfg->sweep || cfg->compare || (cfg->nPatterns > 0);

   /* --load-weights and --rule are compatible but warn if both set */
   if (cfg->loadWeightsFile && cfg->ruleSet) {
      if (!cfg->quiet) {
         fprintf(stderr,
                 "- Note: --load-weights used with --rule; "
                 "rule from weights will be used for learning\n");
      }
   }

   return true;
}

static void free_batch_config(BatchConfig *cfg)
{
   freeMatrix(cfg->comparisonInputs);
   free(cfg->patterns);
   free(cfg->outputFile);
   free(cfg->saveWeightsFile);
   free(cfg->loadWeightsFile);
   memset(cfg, 0, sizeof(BatchConfig));
}

/* Read config file from ./.hopfieldrc or ~/.hopfieldrc */
static bool read_config_file(ConfigFileSettings *settings)
{
   memset(settings, 0, sizeof(ConfigFileSettings));
   settings->noise = -1;

   const char *paths[] = {"./.hopfieldrc", NULL};
   char *home = getenv("HOME");
   char *homePath = NULL;
   if (home) {
      size_t len = strlen(home) + strlen("/.hopfieldrc") + 1;
      homePath = (char *)malloc(len);
      if (homePath) {
         snprintf(homePath, len, "%s/.hopfieldrc", home);
      }
      paths[1] = homePath;
   }

   for (int i = 0; i < 2; i++) {
      if (paths[i] == NULL)
         continue;
      FILE *f = fopen(paths[i], "r");
      if (f == NULL)
         continue;

      char line[MAX_CONFIG_LINE];
      while (fgets(line, sizeof(line), f)) {
         char *p = line;
         while (*p == ' ' || *p == '\t')
            p++;
         if (*p == '#' || *p == '\n' || *p == '\0')
            continue;

         char *eq = strchr(p, '=');
         if (eq == NULL)
            continue;
         *eq = '\0';
         char *key = p;
         char *val = eq + 1;

         /* Trim key */
         char *key_end = key + strlen(key) - 1;
         while (key_end > key && (*key_end == ' ' || *key_end == '\t'))
            *key_end-- = '\0';

         /* Trim val */
         while (*val == ' ' || *val == '\t')
            val++;
         char *val_end = val + strlen(val) - 1;
         while (val_end > val && (*val_end == ' ' || *val_end == '\t' ||
                                  *val_end == '\n' || *val_end == '\r'))
            *val_end-- = '\0';

         if (strcasecmp(key, "rule") == 0) {
            if (strcasecmp(val, "hebbian") == 0)
               settings->rule = RULE_HEBBIAN;
            else if (strcasecmp(val, "storkey") == 0)
               settings->rule = RULE_STORKEY;
            else if (strcasecmp(val, "pseudo-inverse") == 0)
               settings->rule = RULE_PSEUDO_INVERSE;
            else if (strcasecmp(val, "daydreaming") == 0)
               settings->rule = RULE_DAYDREAMING;
            else if (strcasecmp(val, "modern") == 0)
               settings->rule = RULE_MODERN;
            settings->ruleSet = true;
         }
         else if (strcasecmp(key, "seed") == 0) {
            if (!parse_seed(val, &settings->seed)) {
               fprintf(stderr, "Error: invalid config seed '%s'\n", val);
               fclose(f);
               free(homePath);
               return false;
            }
            settings->seedSet = true;
         }
         else if (strcasecmp(key, "noise") == 0) {
            char *endptr;
            long v = strtol(val, &endptr, 10);
            if (endptr != val && *endptr == '\0' && v >= 0 &&
                v <= MAX_NOISE_PERCENT) {
               settings->noise = (int)v;
               settings->noiseSet = true;
            }
         }
         else if (strcasecmp(key, "verbose") == 0) {
            settings->verbose =
               (strcasecmp(val, "true") == 0 ||
                strcasecmp(val, "1") == 0 || strcasecmp(val, "yes") == 0);
         }
         else if (strcasecmp(key, "save_weights") == 0) {
            settings->saveWeightsFile = strdup(val);
         }
         else if (strcasecmp(key, "load_weights") == 0) {
            settings->loadWeightsFile = strdup(val);
         }
         else if (strcasecmp(key, "output") == 0) {
            settings->outputFile = strdup(val);
         }
      }
      fclose(f);
      break; /* Use first found config file */
   }

   free(homePath);
   return true;
}

static bool apply_config_to_batch(const ConfigFileSettings *config,
                                  BatchConfig *cfg)
{
   if (config->ruleSet && !cfg->ruleSet) {
      cfg->rule = config->rule;
      cfg->ruleSet = true;
   }
   if (config->seedSet && !cfg->seedSet) {
      cfg->seed = config->seed;
      cfg->seedSet = true;
   }
   if (config->noiseSet && cfg->noise < 0) {
      cfg->noise = config->noise;
   }
   if (config->verbose && !cfg->verbose) {
      cfg->verbose = true;
   }
   if (config->saveWeightsFile && !cfg->saveWeightsFile) {
      cfg->saveWeightsFile = strdup(config->saveWeightsFile);
   }
   if (config->loadWeightsFile && !cfg->loadWeightsFile) {
      cfg->loadWeightsFile = strdup(config->loadWeightsFile);
   }
   if (config->outputFile && !cfg->outputFile) {
      cfg->outputFile = strdup(config->outputFile);
   }
   return true;
}

static bool open_csv(BatchConfig *cfg)
{
   if (!cfg->csvFile)
      return true;
   cfg->csv = fopen(cfg->csvFile, "w");
   if (cfg->csv == NULL) {
      fprintf(stderr, "Error: Cannot open CSV file '%s'\n", cfg->csvFile);
      return false;
   }
   fprintf(cfg->csv,
           "rule,seed,pattern,reference,noise_percent,neurons,"
           "iterations,converged,overlap,hamming,energy,"
           "training_seconds,recall_seconds,status,trial,corruption,affected_pixels,attractor,matched_pattern,closest1,overlap1,closest2,overlap2,closest3,overlap3,stored_patterns\n");
   return true;
}

static bool close_csv(BatchConfig *cfg)
{
   if (!cfg->csv)
      return true;
   bool success = ferror(cfg->csv) == 0;
   if (fclose(cfg->csv) != 0)
      success = false;
   cfg->csv = NULL;
   if (!success)
      fprintf(stderr, "Error: Failed to write CSV results\n");
   return success;
}

static bool valid_selections(const HopfieldContext *ctx,
                             const BatchConfig *cfg, bool mode2)
{
   int maxPattern = mode2 ? ctx->nNoisyPatterns : ctx->nPatterns;
   for (int i = 0; i < cfg->nPatterns; i++) {
      if (cfg->patterns[i] < 0 || cfg->patterns[i] >= maxPattern) {
         fprintf(stderr,
                 "Error: pattern index %d out of range (1..%d)\n",
                 cfg->patterns[i] + 1, maxPattern);
         return false;
      }
   }
   return true;
}

static int run_batch_mode(HopfieldContext *ctx, BatchConfig *cfg,
                          SimState *state, bool mode2)
{
   bool allConverged = true;
   cfg->trainingSeconds = 0.0;
   cfg->totalOverlap = cfg->totalHamming = cfg->totalIterations = 0.0;
   cfg->totalRecallSeconds = 0.0;
   cfg->exactMatches = cfg->convergedCount = 0;

   /* Determine learning rule */
   LearningRule rule = cfg->ruleSet ? cfg->rule : RULE_HEBBIAN;

   /* Load weights or learn */
   if (cfg->loadWeightsFile) {
      if (!cfg->quiet) {
         printf("- Loading weight matrix from %s .... ",
                cfg->loadWeightsFile);
      }
      if (!hopfield_load_weights(ctx, cfg->loadWeightsFile)) {
         fprintf(stderr, "\n\tERROR: Failed to load weights\n\n");
         return 1;
      }
      if (!cfg->quiet) {
         printf("ready\n\n");
      }
      /* Rule is determined by loaded weights (modernHopfield flag) */
      rule = ctx->modernHopfield ? RULE_MODERN : RULE_HEBBIAN;

      /* Ensure pattern buffers are allocated for recall */
      if (!ensurePatternBuffers(ctx, &state->inputPattern,
                                &state->inputPatternWithNoise,
                                &state->outputPattern)) {
         fprintf(stderr, "Error: Out of memory\n");
         return 1;
      }
   }
   else {
      clock_t trainingStart = clock();
      if (!prepare_network(ctx, rule, state, cfg->quiet)) {
         recordFailure(cfg->record, rule_key(rule), ctx->nPatterns,
                       mode2 ? -1 : cfg->noise, cfg->trial,
                       mode2 ? "file" : noise_key(cfg->corruption));
         return 1;
      }
      cfg->trainingSeconds =
         (double)(clock() - trainingStart) / CLOCKS_PER_SEC;
      if (cfg->saveWeightsFile) {
         if (!cfg->quiet) {
            printf("- Saving weight matrix to %s .... ",
                   cfg->saveWeightsFile);
         }
         if (!hopfield_save_weights(ctx, cfg->saveWeightsFile)) {
            fprintf(stderr, "\n\tERROR: Failed to save weights\n\n");
            return 1;
         }
         if (!cfg->quiet) {
            printf("ready\n\n");
         }
      }
   }

   if (!valid_selections(ctx, cfg, mode2))
      return 3;

   bool ownsCsv = !cfg->compare && !cfg->sweep && !cfg->capacity;
   if (ownsCsv && !open_csv(cfg))
      return 1;

   int result = 0;
   /* Run for each pattern */
   for (int i = 0; i < cfg->nPatterns; i++) {
      bool converged = false;
      bool firstPattern = (i == 0 && cfg->outputFile);
      int ret = run_single_pattern(ctx, i, cfg->patterns[i], cfg->noise, cfg,
                                   state, &converged, firstPattern, mode2);
      if (ret < 0) {
         result = 1;
         break;
      }
      if (!converged)
         allConverged = false;
   }

   if (ownsCsv && !close_csv(cfg))
      result = 1;
   if (result != 0)
      return result;

   if (!cfg->quiet) {
      if (allConverged) {
         printf("\nAll %d pattern(s) converged.\n", cfg->nPatterns);
      }
      else {
         printf("\nSome pattern(s) did not converge.\n");
      }
   }

   if (cfg->sweep || cfg->capacity) {
      SweepTotals *total = &cfg->sweepTotals[cfg->rule];
      total->count += cfg->nPatterns;
      total->exact += cfg->exactMatches;
      total->converged += cfg->convergedCount;
      total->overlap += cfg->totalOverlap;
      total->hamming += cfg->totalHamming;
   }
   return allConverged ? 0 : 2;
}

static int select_defaults(const HopfieldContext *ctx, BatchConfig *cfg,
                           bool mode2)
{
   if (cfg->nPatterns == 0) {
      cfg->nPatterns = mode2 ? ctx->nNoisyPatterns : ctx->nPatterns;
      if ((size_t)cfg->nPatterns > SIZE_MAX / sizeof(int)) {
         fprintf(stderr, "Error: Pattern selection is too large\n");
         return 1;
      }
      cfg->patterns = malloc((size_t)cfg->nPatterns * sizeof(int));
      if (!cfg->patterns) {
         fprintf(stderr, "Error: Out of memory\n");
         return 1;
      }
      for (int i = 0; i < cfg->nPatterns; i++)
         cfg->patterns[i] = i;
   }
   if (!valid_selections(ctx, cfg, mode2))
      return 3;
   return 0;
}

/* Use prefixes of the loaded memories without reallocating their storage.
   Restore metadata before context cleanup, even when an experiment fails. */
static int run_capacity(HopfieldContext *ctx, BatchConfig *cfg,
                        SimState *state)
{
   int originalCount = ctx->nPatterns;
   int result = select_defaults(ctx, cfg, false);
   if (result != 0 || !open_csv(cfg))
      return result != 0 ? result : 1;
   for (int count = 1; count <= originalCount; count++) {
      ctx->nPatterns = count;
      cfg->nPatterns = count;
      memset(cfg->sweepTotals, 0, sizeof(cfg->sweepTotals));
      for (int trial = 0; trial < cfg->trials; trial++) {
         cfg->trial = trial + 1;
         int status = cfg->compare
            ? run_comparison(ctx, cfg, state, false)
            : run_batch_mode(ctx, cfg, state, false);
         if (status == 1 || status == 3) {
            result = 1;
            if (!cfg->compare && cfg->csv)
               fprintf(cfg->csv,
                       "%s,%u,,,%d,,,,,,,,,failed,%d,%s,,,,,,,,,,%d\n",
                       rule_key(cfg->rule), cfg->seed, cfg->noise,
                       cfg->trial, noise_key(cfg->corruption), count);
         }
         else if (status == 2 && result == 0)
            result = 2;
      }
      if (!cfg->quiet) {
         for (LearningRule rule = RULE_HEBBIAN; rule <= RULE_MODERN; rule++) {
            if (!cfg->compare && rule != cfg->rule)
               continue;
            const SweepTotals *t = &cfg->sweepTotals[rule];
            if (t->count > 0.0)
               printf("Capacity %s stored=%d: samples=%.0f "
                      "correct_percent=%.2f mean_overlap=%.4f "
                      "mean_hamming=%.2f\n", rule_key(rule), count,
                      t->count, 100.0 * t->exact / t->count,
                      t->overlap / t->count, t->hamming / t->count);
            else
               printf("Capacity %s stored=%d: no completed samples\n",
                      rule_key(rule), count);
         }
      }
   }
   ctx->nPatterns = originalCount;
   cfg->nPatterns = originalCount;
   if (!close_csv(cfg))
      result = 1;
   return result;
}

static int run_sweep(HopfieldContext *ctx, BatchConfig *cfg,
                     SimState *state)
{
   int result = select_defaults(ctx, cfg, false);
   if (result != 0 || !open_csv(cfg))
      return result != 0 ? result : 1;
   for (int noise = cfg->sweepStart; noise <= cfg->sweepEnd;
        noise += cfg->sweepStep) {
      cfg->noise = noise;
      memset(cfg->sweepTotals, 0, sizeof(cfg->sweepTotals));
      for (int trial = 0; trial < cfg->trials; trial++) {
         cfg->trial = trial + 1;
         int status = cfg->compare
            ? run_comparison(ctx, cfg, state, false)
            : run_batch_mode(ctx, cfg, state, false);
         if (status == 1 || status == 3) {
            result = 1;
            if (!cfg->compare && cfg->csv)
               fprintf(cfg->csv, "%s,%u,,,%d,,,,,,,,,failed,%d,%s,,,,,,,,,,%d\n",
                       rule_key(cfg->rule), cfg->seed, noise, cfg->trial,
                       noise_key(cfg->corruption), ctx->nPatterns);
         }
         else if (status == 2 && result == 0)
            result = 2;
      }
      if (!cfg->quiet) {
         for (LearningRule rule = RULE_HEBBIAN; rule <= RULE_MODERN; rule++) {
            if (!cfg->compare && rule != cfg->rule)
               continue;
            const SweepTotals *t = &cfg->sweepTotals[rule];
            if (t->count > 0.0)
               printf("Sweep %s noise=%d: samples=%.0f correct=%.0f "
                      "correct_percent=%.2f converged=%.0f "
                      "mean_overlap=%.4f mean_hamming=%.2f\n",
                      rule_key(rule), noise, t->count, t->exact,
                      100.0 * t->exact / t->count, t->converged,
                      t->overlap / t->count, t->hamming / t->count);
            else
               printf("Sweep %s noise=%d: no completed samples\n",
                      rule_key(rule), noise);
         }
      }
   }
   if (!close_csv(cfg))
      result = 1;
   return result;
}

/* Build inputs before any training, so stochastic learning cannot change
   the noise presented to another rule. Selection order (including repeats)
   is preserved. srand is still called only once for the whole command. */
static int run_comparison(HopfieldContext *ctx, BatchConfig *cfg,
                          SimState *state, bool mode2)
{
   int selectionStatus = select_defaults(ctx, cfg, mode2);
   if (selectionStatus != 0)
      return selectionStatus;
   if (!mode2) {
      freeMatrix(cfg->comparisonInputs);
      cfg->comparisonInputs = NULL;
      cfg->comparisonInputs = allocMatrix(cfg->nPatterns, ctx->patternSize);
      if (!cfg->comparisonInputs) {
         fprintf(stderr, "Error: Out of memory\n");
         return 1;
      }
      for (int i = 0; i < cfg->nPatterns; i++) {
         int index = cfg->patterns[i];
         if (corruptPattern(ctx, index, cfg->noise, cfg->corruption,
                            cfg->comparisonInputs[i]) < 0) {
            fprintf(stderr, "Error: Failed to allocate noisy input\n");
            return 1;
         }
      }
   }
   if (!cfg->sweep && !cfg->capacity && !open_csv(cfg))
      return 1;

   int result = 0;
   for (LearningRule rule = RULE_HEBBIAN; rule <= RULE_MODERN; rule++) {
      cfg->rule = rule;
      cfg->ruleSet = true;
      cfg->trainingSeconds = 0.0;
      cfg->totalOverlap = 0.0;
      cfg->totalHamming = 0.0;
      cfg->totalIterations = 0.0;
      cfg->totalRecallSeconds = 0.0;
      cfg->exactMatches = 0;
      cfg->convergedCount = 0;
      int status = run_batch_mode(ctx, cfg, state, mode2);
      if (status == 1 || status == 3) {
         result = 1;
         fprintf(stderr, "Comparison %s: failed\n", rule_key(rule));
         if (cfg->csv)
            fprintf(cfg->csv, "%s,%u,,,%d,,,,,,,,,failed,%d,%s,,,,,,,,,,%d\n",
                    rule_key(rule), cfg->seed, cfg->noise, cfg->trial,
                    mode2 ? "file" : noise_key(cfg->corruption),
                    ctx->nPatterns);
         continue;
      }
      if (status == 2 && result == 0)
         result = 2;
      if (!cfg->quiet) {
         double count = cfg->nPatterns;
         printf("Comparison %s: mean_overlap=%.4f mean_hamming=%.2f "
                "exact=%d/%d converged=%d/%d mean_iterations=%.2f "
                "training_seconds=%.6f recall_seconds=%.6f\n",
                rule_key(rule), cfg->totalOverlap / count,
                cfg->totalHamming / count, cfg->exactMatches,
                cfg->nPatterns, cfg->convergedCount, cfg->nPatterns,
                cfg->totalIterations / count, cfg->trainingSeconds,
                cfg->totalRecallSeconds);
      }
   }
   if (!cfg->sweep && !cfg->capacity && !close_csv(cfg))
      result = 1;
   return result;
}

static int run_single_pattern(HopfieldContext *ctx, int selection,
                              int patternIndex, int noisePercent,
                              BatchConfig *cfg,
                              SimState *state, bool *converged,
                              bool firstPattern, bool mode2)
{
   if (mode2) {
      /* Mode 2: use pre-made noisy pattern directly */
      copyPattern(ctx->patternSize, ctx->noisyPatterns[patternIndex],
                  state->inputPattern);
      copyPattern(ctx->patternSize, ctx->noisyPatterns[patternIndex],
                  state->inputPatternWithNoise);

      if (!cfg->quiet) {
         printf("\n\n- Pattern %d as 2D image:\n\n", patternIndex + 1);
      }
   }
   else {
      /* Mode 1: add noise to stored pattern */
      copyPattern(ctx->patternSize, ctx->patterns[patternIndex],
                  state->inputPattern);
      copyPattern(ctx->patternSize, ctx->patterns[patternIndex],
                  state->inputPatternWithNoise);

      if (cfg->comparisonInputs) {
         copyPattern(ctx->patternSize, cfg->comparisonInputs[selection],
                     state->inputPatternWithNoise);
      }
      else {
         if (corruptPattern(ctx, patternIndex, noisePercent,
                            cfg->corruption,
                            state->inputPatternWithNoise) < 0) {
            fprintf(stderr, "Error: Failed to allocate noisy input\n");
            return -1;
         }
      }

      if (!cfg->quiet) {
         printf("\n\n- Pattern %d as 2D image and %d%% noisy pixels:\n\n",
                patternIndex + 1, noisePercent);
      }
   }

   if (cfg->compare && cfg->verbose && !cfg->quiet) {
      printf("Comparison input: rule=%s pattern=%d\n",
             rule_key(cfg->rule), patternIndex + 1);
      showPatternAndDifference(ctx, state->inputPattern,
                               state->inputPatternWithNoise);
   }
   if (!cfg->compare && cfg->corruptionSet && cfg->verbose && !cfg->quiet) {
      printf("Input: corruption=%s pattern=%d (? = unknown)\n",
             noise_key(cfg->corruption), patternIndex + 1);
      showPatternAndDifference(ctx, state->inputPattern,
                               state->inputPatternWithNoise);
   }
   int affected = mode2 ? -1 : calcHammingDistance(
      ctx->patternSize, state->inputPattern, state->inputPatternWithNoise);
   size_t runId = ++cfg->nextRun;
   recordStart(cfg->record, runId, ctx, rule_key(cfg->rule),
               mode2 ? -1 : noisePercent,
               mode2 ? "file" : noise_key(cfg->corruption), cfg->trial,
               patternIndex + 1, mode2, state->inputPattern,
               state->inputPatternWithNoise, cfg->trainingSeconds);
   double finalEnergy;
   struct VerboseCallbackData cb_data = {
      ctx, state->inputPattern, 0, cfg->verbose && !cfg->quiet,
      cfg->record, runId};
   clock_t recallStart = clock();
   *converged = convergePattern(ctx, state->inputPatternWithNoise,
                                state->outputPattern,
                                verbose_iteration_callback, &cb_data,
                                &finalEnergy);

   double recallSeconds = (double)(clock() - recallStart) / CLOCKS_PER_SEC;
   if (cb_data.iterations == 0) {
      fprintf(stderr, "Error: Recall failed before first iteration\n");
      return -1;
   }
   double overlap = calcOverlap(ctx->patternSize, state->inputPattern,
                                state->outputPattern);
   int hamming = calcHammingDistance(
      ctx->patternSize, state->inputPattern, state->outputPattern);
   RecallAnalysis analysis = analyzeRecall(ctx, state->outputPattern,
                                            mode2 ? -1 : patternIndex,
                                            *converged);
   recordResult(cfg->record, runId, state->outputPattern,
                ctx->patternSize, *converged, finalEnergy, overlap,
                hamming, recallSeconds, &analysis);
   if (cfg->record && ferror(cfg->record))
      return -1;
   cfg->totalOverlap += overlap;
   cfg->totalHamming += hamming;
   cfg->totalIterations += cb_data.iterations;
   cfg->totalRecallSeconds += recallSeconds;
   cfg->exactMatches += (hamming == 0);
   cfg->convergedCount += *converged;
   if (cfg->csv) {
      fprintf(cfg->csv,
              "%s,%u,%d,%s,%d,%d,%d,%s,%.17g,%d,%.17g,"
              "%.9f,%.9f,ok,%d,%s,%d",
              cfg->loadWeightsFile ? "loaded" : rule_key(cfg->rule),
              cfg->seed, patternIndex + 1, mode2 ? "noisy" : "stored",
              mode2 ? -1 : noisePercent, ctx->patternSize,
              cb_data.iterations, *converged ? "yes" : "no",
              overlap, hamming, finalEnergy, cfg->trainingSeconds,
              recallSeconds, cfg->trial,
              mode2 ? "file" : noise_key(cfg->corruption), affected);
      fprintf(cfg->csv, ",%s,%d", analysis.kind, analysis.matchedPattern);
      for (int i = 0; i < HOPFIELD_TOP_MATCHES; i++) {
         if (i < analysis.count)
            fprintf(cfg->csv, ",%d,%.17g", analysis.indices[i],
                    analysis.overlaps[i]);
         else
            fprintf(cfg->csv, ",,");
      }
      fprintf(cfg->csv, ",%d\n", ctx->nPatterns);
   }
   if (!cfg->quiet) {
      print_batch_result(patternIndex + 1, overlap, hamming,
                         ctx->patternSize, *converged);
      printRecallAnalysis(&analysis);
   }

   if (cfg->outputFile) {
      if (!save_recall_output(ctx, state->outputPattern, cfg->outputFile,
                              firstPattern)) {
         fprintf(stderr, "\n\tERROR: Failed to save output to %s\n\n",
                 cfg->outputFile);
         return -1;
      }
   }

   return 1;
}

static bool save_recall_output(const HopfieldContext *ctx,
                               const double *pattern, const char *filename,
                               bool firstPattern)
{
   FILE *f = fopen(filename, firstPattern ? "w" : "a");
   if (f == NULL) {
      return false;
   }

   if (!firstPattern) {
      fprintf(f, "\n");
   }

   for (int r = 0; r < ctx->nRows; r++) {
      for (int c = 0; c < ctx->nColumns; c++) {
         fprintf(f, "%c",
                 equals(pattern[r * ctx->nColumns + c], 1.0) ? '*' : '.');
      }
      fprintf(f, "\n");
   }

   bool success = ferror(f) == 0;
   if (fclose(f) != 0)
      success = false;
   return success;
}

static void print_batch_result(int patternIdx, double overlap, int hamming,
                               int patternSize, bool converged)
{
   printf("  Pattern %d: overlap=%7.4f hamming=%d/%d converged=%s\n",
          patternIdx, overlap, hamming, patternSize,
          converged ? "yes" : "no");
}

static void print_usage(void)
{
   printf("Usage: hopfieldann <patterns.dat> [noisy.dat] [options]\n\n");
   printf("Options:\n");
   printf(
      "  -r, --rule RULE           Learning rule: hebbian, storkey, "
      "pseudo-inverse,\n");
   printf(
      "                            daydreaming, modern (default: "
      "prompt)\n");
   printf(
      "  -p, --pattern LIST        Pattern indices 1..N, comma-separated "
      "(e.g. 1,3,5)\n");
   printf(
      "  -n, --noise PERCENT       Noise level 0..100 (mode 1 only)\n");
   printf("  -s, --seed VALUE          Random seed for reproducibility\n");
   printf("      --corruption TYPE     flip [default], erase, block, "
          "left, right, top, bottom\n");
   printf("      --record FILE        Save replay data for the offline viewer\n");
   printf("      --capacity           Test growing prefixes of stored memories\n");
   printf("      --sweep A:B:S         Noise range, inclusive bound, step S\n");
   printf("      --trials N            Repetitions per sweep level [10]\n");
   printf("      --compare            Compare all five learning rules\n");
   printf("  -c, --csv FILE            Export batch metrics as CSV\n");
   printf("  -q, --quiet               Suppress non-error output\n");
   printf("  -v, --verbose             Show energy per iteration\n");
   printf("  -o, --output FILE         Save recall pattern to FILE\n");
   printf(
      "  -w, --save-weights FILE   Save weight matrix after learning "
      "(.bin)\n");
   printf(
      "  -l, --load-weights FILE   Load weight matrix, skip learning\n");
   printf("  -h, --help                Show this help\n\n");
   printf(
      "Config file: ./.hopfieldrc or ~/.hopfieldrc (key=value, # "
      "comments)\n");
   printf(
      "  rule, seed, noise, verbose, save_weights, load_weights, "
      "output\n\n");
   printf("Interactive mode: omit --pattern, --compare and --sweep.\n");
}