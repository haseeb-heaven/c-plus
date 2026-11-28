/* Consumer: additive overlay "stdlib/safe/safe_c.h" (opt-in macro layer).
 * Ordinary malloc/memset/free spellings are redirected to checked entry
 * points; no source rewrite to _s names is required. */
#include "stdlib/safe/safe_c.h"
int stdc_safe_c_probe(void)
{
    void *p = malloc(32);
    int ok = (p != NULL);
    memset(p, 0, 32);
    free(p);
    return ok;
}
