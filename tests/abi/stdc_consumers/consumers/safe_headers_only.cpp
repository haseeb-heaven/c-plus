/* Supplementary probe: the safeclib-style overlay headers are C++-parseable
 * and declare their APIs with C linkage. This deliberately does NOT include
 * cplus_libc.h, isolating it as the only C++ blocker in safe_c.h. */
#include "safe_alloc.h"
#include "safe_memory.h"
#include "safe_string.h"
#include "safe_io.h"
#include "safe_format.h"
int stdc_safe_headers_only_probe()
{
    void *p = malloc_s(32);
    (void)cplus_memset_s_auto(p, 0, 32, 32);
    (void)cplus_strcpy_s_auto(static_cast<char *>(p), "x", 32);
    free_s(p);
    return p != nullptr;
}
