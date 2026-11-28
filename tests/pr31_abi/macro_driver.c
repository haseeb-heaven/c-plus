/* PR31 fixture: ordinary ISO C spelling remapped by cplus_libc_safe.h.
 *
 * Proves the header-level macro path used by plain C consumers still hands a
 * cplus-owned block to a different TU's checked release entry point.
 */
#define PR31_USE_CPLUS_HEADER 1
#include "cplus_libc_safe.h"
#include "pr31_shared.h"

#include <stdio.h>

static int g_pass;
static int g_fail;

static void ck(const char *name, int ok)
{
    printf("PR31_MACRO %s %s\n", name, ok ? "PASS" : "FAIL");
    if (ok)
        ++g_pass;
    else
        ++g_fail;
}

int main(void)
{
    void *p = malloc(64); /* -> cplus_malloc via cplus_libc_safe.h */
    void *slot = p;
    int rc;

    if (p != NULL)
        ((char *)p)[0] = 'm';
    rc = pr31_a_release_checked(&slot);
    ck("macro_malloc_released_in_other_tu",
       p != NULL && rc == 0 && slot == NULL);

    p = cplus_malloc(32);
    free(p); /* -> cplus_free via cplus_libc_safe.h */
    ck("macro_free_of_cplus_block", p != NULL && cplus_alloc_is_live(p) == 0);

    printf("PR31_MACRO_RESULT %d %d\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
