/* Deterministic pseudo-random allocator churn under the safe runtime. */
#include <stdint.h>
#include <stdio.h>
#include "cplus/cplus.h"
#define SLOTS 128
#define ROUNDS 5000
static uint64_t state = 0x12345678ULL;
static uint64_t next(void) {
  state = state * 6364136223846793005ULL + 1442695040888963407ULL;
  return state >> 33;
}
int main(void) {
  void *slots[SLOTS] = {0};
  size_t sizes[SLOTS] = {0};
  for (int r = 0; r < ROUNDS; r++) {
    size_t i = (size_t)(next() % SLOTS);
    if (slots[i]) {
      cplus_check_live(slots[i]);
      if (cplus_alloc_bytes(slots[i]) != sizes[i]) return 1;
      cplus_release(slots[i]);
      slots[i] = NULL;
    } else {
      size_t n = 1 + (size_t)(next() % 64);
      slots[i] = cplus_alloc(n);
      sizes[i] = n;
    }
  }
  for (size_t i = 0; i < SLOTS; i++)
    if (slots[i]) cplus_release(slots[i]);
  print("fuzz", SLOTS);
  return 0;
}
