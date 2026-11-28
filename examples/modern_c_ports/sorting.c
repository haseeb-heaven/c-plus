/* Independent modern-C port of the "sorting" the inspiration language sample workload.
 * C11, no the inspiration language source copied. See proof/modern_c_ports/README.md. */
#include "common.h"

int main(void) {
    int v[8] = {7, 3, 8, 1, 6, 2, 5, 4}, i;
    qsort(v, 8, sizeof(int), cmp_int);
    for (i = 0; i < 8; ++i) printf("sorting %d\n", v[i]);
    CHECK("sorting_order", v[0] == 1 && v[7] == 8); report("SORTING"); return fails;
}
