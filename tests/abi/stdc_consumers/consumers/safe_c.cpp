/* C++17 consumer of the additive "stdlib/safe/safe_c.h" overlay.
 * Verifies the safeclib-style wrapper headers expose extern "C" linkage. */
#include "stdlib/safe/safe_c.h"
int stdc_safe_c_probe()
{
    void *p = malloc(32);
    int ok = (p != NULL);
    memset(p, 0, 32);
    free(p);
    return ok;
}
