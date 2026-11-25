/* Independent modern-C port of the "tour" Goose sample workload.
 * C11, no Goose source copied. See proof/goose_samples/README.md. */
#include "common.h"

int main(void) {
    int i, sum = 0;
    for (i = 1; i <= 5; ++i) { printf("tour %d\n", i); sum += i; }
    CHECK("tour_sum", sum == 15); report("TOUR"); return fails;
}
