/* tests/c_standards/c99/c99_features.c
 *
 * A minimal, genuine C99 program exercising features introduced by
 * ISO/IEC 9899:1999 (C99) that are NOT available in C90/C94:
 *
 *   1. // single-line comments
 *   2. mixed declarations (a declaration appearing after a statement)
 *   3. <stdint.h> fixed-width integer types (uint32_t / int64_t)
 *   4. a declaration inside a for-loop init-clause
 *
 * The program is self-contained and produces deterministic stdout so that a
 * reference (/usr/bin/clang) build and a C+ driver build can be compared.
 */
#include <stdio.h>
#include <stdint.h>

int main(void) {
  uint32_t u = 0xDEADBEEFu; /* #1 <stdint.h> type + #1 // comment style below. */
  printf("C99 u=%08x\n", (unsigned)u); // // comment (C99-only)

  int sum = 0; /* #2 mixed declaration: appears after the printf statement. */
  for (int i = 0; i < 4; ++i) { /* #4 for-loop declaration (C99-only). */
    sum += i;
  }
  printf("C99 sum=%d\n", sum);

  int64_t big = -1234567890123LL; /* #2 another mixed declaration. */
  printf("C99 big=%lld\n", (long long)big);
  return 0;
}
