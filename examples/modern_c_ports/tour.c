/* Independent modern-C port of the "tour" the inspiration language sample workload.
 * C11, no the inspiration language source copied. See proof/modern_c_ports/README.md. */
#include "common.h"

int main(void) {
    int i, sum = 0;
    for (i = 1; i <= 5; ++i) { printf("tour %d\n", i); sum += i; }
    CHECK("tour_sum", sum == 15); report("TOUR"); return fails;
}
