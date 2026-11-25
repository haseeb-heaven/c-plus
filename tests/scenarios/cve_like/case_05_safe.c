/* BASELINE: case_05.c crashes; SAFE: this file reports a defined refusal. */
/* Layer: default = SafeCRT ordinary C names (stdlib/safe/safe_c.h).
 *        -DPLAIN_LIBC = standalone hardened libc (cplus_libc_safe.h). */
#include <stdint.h>
#include <stddef.h>
#include <errno.h>
#ifdef PLAIN_LIBC
#include "cplus_libc_safe.h"
#else
#include "stdlib/safe/safe_c.h"
#include "safe_runtime.h"
#endif

struct Matrix { uint32_t rows, cols; double *cells; };

int main(void) {
    struct Matrix m = {0, 0, NULL};

#ifdef CPLUS_SAFE_C_ENABLE
    cplus_runtime_set_violation_trap(0);
#endif

    printf("rows cols: ");
    if (scanf("%u %u", &m.rows, &m.cols) != 2) {
        printf("case_05: refused (no valid dimensions)\n");
        return 0;
    }
    if (m.rows == 0 || m.cols == 0) {
        printf("case_05: refused (zero dimension)\n");
        return 0;
    }

    /* Caller check the raw C version omits: `uint32_t total = rows*cols`
     * wraps (65536*65536 -> 0) while the last-element index is computed in
     * 64-bit arithmetic, so a tiny allocation is indexed far out of range.
     * Compute the product in 64 bits and prove it fits size_t before any
     * cast to a smaller type. */
    uint64_t cells64 = (uint64_t)m.rows * (uint64_t)m.cols;
    if (cells64 > (uint64_t)UINT32_MAX) {
        printf("case_05: refused (32-bit element-count product overflow)\n");
        return 0;
    }
    if (cells64 > (uint64_t)(SIZE_MAX / sizeof(double))) {
        printf("case_05: refused (element count overflows size_t)\n");
        return 0;
    }
    size_t total = (size_t)cells64;

    /* Library check: calloc(n,size) == calloc_s / cplus_calloc rejects a
     * wrapping n*size product with EOVERFLOW instead of under-allocating. */
    m.cells = calloc(total, sizeof(double));
    if (!m.cells) {
        printf("case_05: refused (allocation overflow or OOM)\n");
        return 0;
    }

    /* Caller check: the original computes this index with uint64_t but
     * indexes an allocation sized by the wrapped 32-bit product.  Verify
     * the computed index is within the validated element count before the
     * direct (unbounds-checked) pointer-index write. */
    uint64_t last = (uint64_t)(m.rows - 1u) * m.cols + (m.cols - 1u);
    if (last >= cells64) {
        printf("case_05: refused (computed index out of range)\n");
        free(m.cells);
        return 0;
    }
    m.cells[last] = 3.14159;

    free(m.cells);
    printf("case_05: ok (rows=%u cols=%u)\n", m.rows, m.cols);
    return 0;
}
