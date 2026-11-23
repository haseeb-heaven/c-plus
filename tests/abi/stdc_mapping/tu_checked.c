/* Compiled WITH -include stdlib/safe/safe_c.h (force-included by verify.sh).
 * The ordinary spellings in THIS TU only must be rewritten to checked targets.
 * No function is defined here; the header supplies declarations only. */
#include <stdlib.h>

void *tu_checked_alloc(size_t n) { return malloc(n); }
void tu_checked_free(void *p) { free(p); }
