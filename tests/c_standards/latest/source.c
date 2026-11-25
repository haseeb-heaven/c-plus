/* tests/c_standards/latest/source.c
 *
 * Minimal, self-contained probe for the newest -std spellings that
 * Apple clang 16.0.0 actually accepts (c2x / gnu2x, i.e. the C23 draft
 * mode reporting __STDC_VERSION__ == 202000L).
 *
 * Design notes (all deliberate):
 *   - It uses only C23 features that BOTH plain clang and the C+ driver
 *     accept: bool/true as keywords, a binary literal, and typeof.
 *   - It deliberately AVOIDS features the C+ frontend rejects or hangs on
 *     (nullptr, _BitInt, digit separators).  Those are probed separately
 *     by run.sh so the limitations are recorded rather than hidden.
 *   - Output is deterministic so plain-clang and C+ stdout can be compared
 *     byte-for-byte.
 *
 * Reference build:
 *   /usr/bin/clang -std=c2x source.c -o ref
 *   ./build/c+ build source.c -std=c2x --sanitize=none -o cplus
 */

#include <stdio.h>

#ifndef __STDC_VERSION__
#define __STDC_VERSION__ 0L
#endif

static void report_c23_features(void)
{
#if __STDC_VERSION__ >= 202000L
  /* C23: bool/true are keywords; 0b... binary literals; typeof. */
  bool ready = true;
  int mask = 0b1011;
  typeof(mask) copy = mask;
  printf("C23FEATURES ready=%d mask=%d copy=%d\n",
         (int)ready, mask, copy);
#else
  printf("C23FEATURES unavailable\n");
#endif
  printf("VER=%ld\n", (long)__STDC_VERSION__);
}

int main(void)
{
  report_c23_features();
  return 0;
}
