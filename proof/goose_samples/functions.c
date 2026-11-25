/* Independent modern-C port of the "functions" Goose sample workload.
 * C11, no Goose source copied. See proof/goose_samples/README.md. */
#include "common.h"

static unsigned fact(unsigned n) { return n <= 1u ? 1u : n * fact(n - 1u); }
int main(void) {
    CHECK("functions_0", fact(0) == 1u);
    CHECK("functions_1", fact(1) == 1u);
    CHECK("functions_5", fact(5) == 120u);
    printf("functions %u\n", fact(5)); report("FUNCTIONS"); return fails;
}
