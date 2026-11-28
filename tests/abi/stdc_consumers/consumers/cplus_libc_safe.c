/* Consumer: additive overlay "cplus_libc_safe.h" (opt-in macro layer).
 * Ordinary C spellings are preserved at the source level: malloc/memset/free
 * are redirected by the overlay to the checked cplus_* entry points. */
#include "cplus_libc_safe.h"
int stdc_cplus_libc_safe_probe(void)
{
    void *p = malloc(16);
    int ok = (p != NULL);
    memset(p, 0, 16);
    cplus_libc_set_unknown_capacity_mode(0);
    free(p);
    return ok;
}
