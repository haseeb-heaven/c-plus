/* Gate 09: integer overflow (size multiplication wrapping).
 * Expected outcome class: runtime_reject via checked API (allocated count != intended count).
 * C+ note: size_t multiplication wraps with DEFINED semantics, so UBSan does NOT
 * flag it. calloc_s would refuse the wrapped product directly; this case instead
 * detects the consequence (wrapped small allocation) via cplus_safe_query_length.
 */
#include <stdio.h>
#include "stdlib/safe/safe_c.h"

int main(void) {
    /* count * 8 wraps mod 2^64 to 8 on this target. */
    size_t count = ((size_t)1 << (sizeof(size_t) * 8 - 3)) + 1u;
    size_t bytes = count * 8u;
    unsigned char *p = malloc_s(bytes);
    size_t actual = 0u;
    if (p == NULL) {
        printf("gate09: allocation failed\n");
        return 0;
    }
    if (cplus_safe_query_length(p, 8u, &actual) != 0 || actual != count) {
        printf("gate09: REJECTED wrapped size %lu != intended count %lu\n",
               (unsigned long)bytes, (unsigned long)count);
        free_s(p);
        return 1;
    }
    printf("gate09: accepted (no wrap detected)\n");
    free_s(p);
    return 0;
}
