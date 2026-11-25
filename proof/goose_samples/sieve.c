/* Independent modern-C port of the "sieve" Goose sample workload.
 * C11, no Goose source copied. See proof/goose_samples/README.md. */
#include "common.h"

int main(void) {
    int prime[31] = {0}, i, j, count = 0;
    for (i = 2; i <= 30; ++i) prime[i] = 1;
    for (i = 2; i <= 30; ++i) if (prime[i])
        for (j = i * i; j <= 30; j += i) prime[j] = 0;
    for (i = 2; i <= 30; ++i) if (prime[i]) { printf("sieve %d\n", i); count++; }
    CHECK("sieve_count", count == 10); report("SIEVE"); return fails;
}
