/* Bug 2: use-after-free. C+ cannot instrument every raw dereference, but an
 * explicit cplus_check_live() detects the dangling state safely. */
#include "cplus/cplus.h"
int main(void) {
    int *ptr = (int *)cplus_alloc(sizeof(int));
    if (!ptr) return 1;
    *ptr = 42;
    cplus_release(ptr);
    cplus_check_live(ptr);   /* fatal: pointer is no longer live */
    return 0;
}
