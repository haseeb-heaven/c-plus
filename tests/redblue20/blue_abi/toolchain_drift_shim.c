/* Test-only shim for BLUE20 ABI proof.
 *
 * Root cause: the C+ driver in this environment is a wrapper into an external
 * "c-plus-core" checkout that is NEWER than this repository's runtime archive.
 * The driver lowers integer addition to a call to __cplus_checked_add_i64(), a
 * helper added in that newer core, but this repo's build/libcplus_runtime.a
 * (built from this repo's older runtime/cplus_runtime.c) does not define it.
 * The official tests/abi/run_abi_compat.sh C-driver link line therefore fails
 * with "Undefined symbols: ___cplus_checked_add_i64" -- a TOOLCHAIN DRIFT
 * issue, not a C ABI/layout/symbol regression (the ABI fingerprint matches).
 *
 * This shim provides ONLY that one missing helper so the unchanged C-driver
 * source (tests/abi/abi_compat_c_driver.c) can be linked and its 13 C<->C+
 * boundary checks executed. Semantics mirror the external core's definition
 * (runtime/cplus_runtime.c: __int128 addition with a 64-bit range check).
 * It is NOT linked into any shipped library and is not a claim that the repo's
 * runtime archive defines this symbol.
 */
#include <stdint.h>
#include <stdlib.h>

/* Mirrors runtime/include/cplus/cplus.h:cplus_check_nonnull(). Valid pointers
 * simply return; a NULL pointer aborts (the real runtime prints a fatal
 * diagnostic, which is irrelevant to these exercised valid-input checks). */
void cplus_check_nonnull(const void *ptr, const char *file, int line,
                         const char *expr)
{
    (void)file;
    (void)line;
    (void)expr;
    if (ptr == NULL)
        abort();
}

int64_t __cplus_checked_add_i64(int64_t a, int64_t b, unsigned bits,
                                unsigned isSigned)
{
    __int128 r = (__int128)a + (__int128)b;
    if (bits == 64) {
        if (isSigned) {
            if (r > (__int128)INT64_MAX || r < (__int128)INT64_MIN)
                abort();
        } else {
            if (r > (__int128)UINT64_MAX || r < 0)
                abort();
        }
    }
    return (int64_t)r;
}
