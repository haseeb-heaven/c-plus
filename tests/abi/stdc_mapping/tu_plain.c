/* Compiled WITHOUT -include safe_c.h: ordinary spellings must stay bound to
 * host CRT symbols. Used for the per-TU / no-interposition / precompiled-lib
 * evidence in verify.sh. */
#include <stdlib.h>
#include <string.h>

void *tu_plain_alloc(size_t n) { return malloc(n); }
size_t tu_plain_strlen(const char *s) { return strlen(s); }
void tu_plain_free(void *p) { free(p); }
