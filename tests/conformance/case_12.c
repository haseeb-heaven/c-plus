/* Gate 12: FFI/unsafe boundary (calling into external code with an unchecked contract).
 * Expected outcome class: runtime_reject (raw SIGSEGV; no detector in plain C).
 * C+ note: C has no `unsafe` marker and no contract enforcement at this ABI
 * boundary; the crash is the only signal. UBSan does not check external
 * preconditions. The Rust pair marks the same call with `unsafe`.
 */
#include <stdio.h>

/* External C ABI routine. Contract: data must be non-NULL. */
int external_sum(const int *data, unsigned long len) {
    int total = 0;
    unsigned long i;
    for (i = 0; i < len; ++i) total += data[i];
    return total;
}

int main(void) {
    volatile unsigned long len = 4;
    printf("gate12: calling external ABI with NULL data (contract violation)\n");
    fflush(stdout);
    printf("gate12: total=%d\n", external_sum((const int *)0, len));
    return 0;
}
