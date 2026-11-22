/* Gate 04: use-after-free (alias written after release).
 * Expected outcome class: runtime_reject via checked API (recent-free cache + live table).
 * C+ note: this is runtime diagnosis for blocks from this allocator only; a stale
 * alias whose address has been re-issued is not provably distinguishable. UBSan
 * does NOT detect use-after-free (ASan would), so no sanitizer is relied on.
 */
#include <stdio.h>
#include <string.h>
#include "stdlib/safe/safe_c.h"

int main(void) {
    unsigned char *p = (unsigned char *)malloc_s(16u);
    unsigned char *alias = p;
    if (p == NULL) {
        printf("gate04: allocation failed\n");
        return 0;
    }
    memset(p, 0x11, 16u);
    free_s(p);                          /* owner releases the block */
    if (cplus_safe_ptr_was_freed(alias) && !cplus_safe_ptr_is_live(alias)) {
        printf("gate04: REJECTED use-after-free alias detected before write\n");
        return 1;
    }
    printf("gate04: accepted (alias still reported live)\n");
    return 0;
}
