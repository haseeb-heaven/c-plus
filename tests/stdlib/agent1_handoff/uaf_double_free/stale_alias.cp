/* P0 reproducer: pointer alias captured before drop(), used after drop().
 *
 * EXPECT: REJECT with a tagged C+ diagnostic, e.g.
 *   [CPLUS-LIFETIME] [CPLUS-SAFE-LIFETIME] use-after-free:
 *       liveness check on released pointer 'p'
 *
 * `p` aliases the buffer owned by `s` while `s` is alive.  s.drop() frees
 * that buffer, so `p` is a stale alias afterwards.  The invalid use is
 * expressed through cplus_check_live(), the checked API that proves a
 * managed pointer is still live, instead of a raw dereference; on a compile
 * that misses the bug the call fails safely at run time rather than reading
 * freed memory.
 *
 * OBSERVED with the current build/c+: ACCEPT (capability gap).  Control:
 * the identical cplus_check_live() pattern on a directly cplus_alloc-ated
 * pointer IS rejected with a tagged diagnostic (see NOTES.md).
 */
#include "cplus/cplus.h"
#include "stdlib/string.hp"

int main(void) {
    string s = string("abc");
    const char *p = s.c_str(); /* captured while owner is alive */
    s.drop();                  /* frees the buffer `p` aliases */
    cplus_check_live((void *)p); /* checked use of the stale alias */
    return 0;
}
