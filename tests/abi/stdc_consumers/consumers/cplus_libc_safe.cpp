/* C++17 consumer of the additive "cplus_libc_safe.h" overlay.
 * Also exercises the extern "C" linkage of the cplus_* declarations. */
#include "cplus_libc_safe.h"
int stdc_cplus_libc_safe_probe()
{
    void *p = malloc(16);
    int ok = (p != NULL);
    memset(p, 0, 16);
    cplus_libc_set_unknown_capacity_mode(0);
    free(p);
    return ok;
}
