/* Gate 11: function-pointer/indirect-call validation (OOB dispatch via runtime index).
 * Expected outcome class: runtime_reject via checked API (allocator length query).
 * C+ note: validation uses live allocation metadata for heap function-pointer
 * tables; a raw stack table with a caller-supplied wrong length cannot be
 * validated by the library. No diagnostic directives are used.
 */
#include <stdio.h>
#include "stdlib/safe/safe_c.h"

typedef int (*fp)(void);

static int one(void)   { return 1; }
static int two(void)   { return 2; }
static int three(void) { return 3; }

int main(void) {
    fp *table = (fp *)calloc_s(3u, sizeof(fp));
    size_t n = 0u;
    volatile int idx = 10;
    if (table == NULL) {
        printf("gate11: allocation failed\n");
        return 0;
    }
    table[0] = one;
    table[1] = two;
    table[2] = three;
    if (cplus_safe_query_length(table, sizeof(fp), &n) != 0 || (size_t)idx >= n) {
        printf("gate11: REJECTED indirect-call index %d out of range (len %lu)\n",
               idx, (unsigned long)n);
        free_s(table);
        return 1;
    }
    printf("gate11: dispatch=%d\n", table[idx]());
    free_s(table);
    return 0;
}
