/* Supplementary isolation probe: include cplus_libc.h directly from C++17.
 * Expected to FAIL on the C-only `restrict` / `double complex` tokens; this
 * isolates the root cause seen in the cplus_libc_safe.cpp and safe_c.cpp
 * consumers. */
#include "cplus_libc.h"
int stdc_cplus_libc_only_probe()
{
    void *p = cplus_malloc(16);
    cplus_free(p);
    return p != nullptr;
}
