/* Regression: the retired-address ledger must stay linear once it overflows.
 *
 * The runtime keeps a bounded ledger of freed addresses so a second free of
 * the same block is reported as a double free (exit 104).  The ledger holds
 * CPLUS_RETIRED_MAX (1048576) addresses; past that point every retirement
 * evicts an older entry instead of being dropped.
 *
 * The eviction victims used to be picked with a sequential cursor, which
 * emptied one dense band of slots.  A dense band collapses the open-addressed
 * probe chains, so every later retirement rescanned the band and the ledger
 * turned quadratic: retiring 2.4M addresses took 67 seconds instead of 0.8.
 * The victims are now scattered with a multiplicative stride.
 *
 * The test retires more addresses than the cap allows, so the eviction path
 * runs, and then checks that
 *   1. the ledger reports itself full,
 *   2. the most recent retirement is still detectable as a double free,
 *   3. the retirements took linear, not quadratic, time.
 *
 * The addresses must be distinct, so all blocks stay live until they are all
 * released: a freed block would be handed back by malloc and reused, and the
 * ledger would see the same address twice instead of a new retirement.
 */

#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "cplus/cplus.h"

#ifndef LEDGER_SCALE
#define LEDGER_SCALE 2400000u
#endif

/* Seconds the release phase may take.  The fixed runtime needs well under a
 * second here; the quadratic one needed more than a minute. */
#ifndef LEDGER_BUDGET_SECONDS
#define LEDGER_BUDGET_SECONDS 30.0
#endif

static size_t env_size(const char *name, size_t fallback) {
  const char *raw = getenv(name);
  if (!raw || raw[0] == '\0')
    return fallback;
  return (size_t)strtoull(raw, NULL, 10);
}

static double env_double(const char *name, double fallback) {
  const char *raw = getenv(name);
  if (!raw || raw[0] == '\0')
    return fallback;
  return strtod(raw, NULL);
}

static double elapsed_seconds(const struct timespec *from,
                              const struct timespec *to) {
  return (double)(to->tv_sec - from->tv_sec) +
         (double)(to->tv_nsec - from->tv_nsec) / 1e9;
}

int main(void) {
  const size_t scale = env_size("CPLUS_LEDGER_SCALE", (size_t)LEDGER_SCALE);
  const double budget = env_double("CPLUS_LEDGER_BUDGET",
                                   (double)LEDGER_BUDGET_SECONDS);
  void **blocks = (void **)malloc(scale * sizeof(void *));
  struct timespec start;
  struct timespec stop;
  int ledger_full;
  int last_retired;
  double seconds;

  if (!blocks) {
    fprintf(stderr, "out of memory for %zu block pointers\n", scale);
    return 2;
  }
  for (size_t i = 0; i < scale; ++i) {
    blocks[i] = cplus_alloc(32);
    if (!blocks[i]) {
      fprintf(stderr, "allocation failed at index %zu\n", i);
      return 2;
    }
  }

  clock_gettime(CLOCK_MONOTONIC, &start);
  for (size_t i = 0; i < scale; ++i)
    cplus_release(blocks[i]);
  clock_gettime(CLOCK_MONOTONIC, &stop);
  seconds = elapsed_seconds(&start, &stop);

  ledger_full = cplus_retired_ledger_full();
  last_retired = cplus_address_retired(blocks[scale - 1]);

  printf("scale %zu\n", scale);
  printf("ledger_full %d\n", ledger_full);
  printf("last_retired %d\n", last_retired);
  printf("linear %d\n", seconds <= budget ? 1 : 0);
  fprintf(stderr, "retired %zu addresses in %.2fs (budget %.1fs)\n",
          scale, seconds, budget);

  free(blocks);
  if (!ledger_full || !last_retired || seconds > budget) {
    fprintf(stderr,
            "retired-ledger regression: ledger_full=%d last_retired=%d "
            "seconds=%.1f (budget %.1f)\n",
            ledger_full, last_retired, seconds, budget);
    return 1;
  }
  return 0;
}
