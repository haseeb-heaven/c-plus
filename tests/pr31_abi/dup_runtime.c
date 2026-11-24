/* PR31 fixture: two independent allocator metadata instances in one process.
 *
 * The second instance is produced by compiling libc/src/stdlib/alloc.c with
 * every exported cplus_* symbol renamed through -D flags (see run.sh).  Its
 * static side table is therefore distinct from the archive's table.  macOS has
 * no dlmopen(3), so this is the closest feasible duplicate-runtime experiment;
 * the limitations are stated explicitly in the report.
 */
#include "pr31_shared.h"

#include <errno.h>
#include <stdio.h>

extern void *dup_cplus_malloc(size_t);
extern void dup_cplus_free(void *);
extern int dup_cplus_alloc_is_live(const void *);

static int g_pass;
static int g_fail;

static void ck(const char *name, int ok)
{
    printf("PR31_DUP %s %s\n", name, ok ? "PASS" : "FAIL");
    if (ok)
        ++g_pass;
    else
        ++g_fail;
}

int main(void)
{
    void *p1 = cplus_malloc(64);
    void *p2 = dup_cplus_malloc(64);
    void *foreign_to_first = dup_cplus_malloc(48);
    void *slot = foreign_to_first;
    int rc;

    ck("two_instance_allocate", p1 != NULL && p2 != NULL && foreign_to_first != NULL);
    ck("tables_are_independent",
       cplus_alloc_is_live(p1) == 1 && dup_cplus_alloc_is_live(p1) == 0 &&
       dup_cplus_alloc_is_live(p2) == 1 && cplus_alloc_is_live(p2) == 0);

    /* Instance 1's checked free refuses a block owned by instance 2; the
     * pointer slot is nulled on refusal, so keep the original address. */
    errno = 0;
    rc = cplus_free_checked(&slot);
    ck("foreign_instance_free_refused",
       rc == EINVAL && errno == EINVAL && slot == NULL &&
       dup_cplus_alloc_is_live(foreign_to_first) == 1);
    dup_cplus_free(foreign_to_first);

    cplus_free(p1);
    dup_cplus_free(p2);
    ck("each_instance_frees_own_block",
       cplus_alloc_is_live(p1) == 0 && dup_cplus_alloc_is_live(p2) == 0);

    printf("PR31_DUP_RESULT %d %d\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
