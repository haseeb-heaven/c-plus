/* Gate 03: allocation/deallocation safety (oversized/failed allocation then safe free).
 * Expected outcome class: accepted (the checked allocator refuses the request;
 * free_s(NULL) is a defined no-op). No sanitizer is involved in this detection.
 */
#include <stdio.h>
#include <errno.h>
#include "stdlib/safe/safe_c.h"

int main(void) {
    /* count * size exceeds RSIZE_MAX_MEM, so calloc_s must refuse it. */
    void *p = calloc_s((size_t)-1, 8u);
    if (p == NULL) {
        printf("gate03: SAFE oversized allocation refused (errno=%d); free_s(NULL) safe\n", errno);
        free_s(p);
        return 0;
    }
    printf("gate03: UNSAFE oversized allocation returned non-NULL\n");
    free_s(p);
    return 1;
}
