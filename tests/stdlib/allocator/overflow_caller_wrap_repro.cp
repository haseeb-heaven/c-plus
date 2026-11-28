/* KNOWN-GAP REPRO (not a conformance test; see OVERFLOW_NOTES.md).
 *
 * Minimal reproduction of a caller-side allocation-size wrap that the current
 * --strict front end accepts: the arithmetic wraps BEFORE the checked
 * allocator is entered, so stdlib_alloc() sees a small, perfectly valid size
 * (7 and 4 bytes below) and cannot detect the original overflow.  The checked
 * allocator itself is behaving correctly; rejecting this expression belongs
 * to the compiler's strict arithmetic checker (outside this phase's file
 * ownership).  This file records the observed behaviour in both verification
 * modes so the gap is reproducible; it exits 0 and does not assert a pass.
 */
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"

static size_t ovr_opaque(size_t v) { return v; }

int main(void) {
    void *p;
    size_t n;
    size_t m;

    stdlib_alloc_fail_reset();
    n = ovr_opaque(SIZE_MAX - 8u);
    p = stdlib_alloc(n + 16u); /* wraps to 7 */
    print("caller_add_wrap_accepted", p != NULL && stdlib_alloc_size(p) == 7u);
    if (p != NULL)
        stdlib_release(p);

    m = ovr_opaque(SIZE_MAX / 4u + 1u);
    p = stdlib_alloc(m * 4u); /* 2^64 wraps to 0 -> stdlib_alloc's 1-byte rule */
    print("caller_mul_wrap_accepted", p != NULL && stdlib_alloc_size(p) == 1u);
    if (p != NULL)
        stdlib_release(p);

    p = stdlib_alloc(SIZE_MAX - 8u + 16u); /* constant-folded wrap to 7 */
    print("caller_const_wrap_accepted", p != NULL && stdlib_alloc_size(p) == 7u);
    if (p != NULL)
        stdlib_release(p);
    return 0;
}
