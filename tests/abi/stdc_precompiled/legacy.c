/* Precompiled standard-C library (the compatibility baseline).
 *
 * This translation unit is compiled EXACTLY ONCE by /usr/bin/clang with
 *   -O2 -std=c11 -c
 * and archived into libstdc_legacy.a.  It sees no C+ header and no safety
 * layer: every call below uses an ordinary ISO C name and binds to host libc
 * (or clang builtins) at link time.  The resulting object's bytes and ABI must
 * stay identical whether or not a *caller* opts into stdlib/safe/safe_c.h.
 */
#include <stddef.h>
#include <string.h>

int stdc_legacy_version(void) { return 11; }

int stdc_legacy_mul(int a, int b) { return a * b; }

int stdc_legacy_sum(const unsigned char *data, size_t n) {
    size_t i;
    int s = 0;
    if (!data) return -1;
    for (i = 0; i < n; ++i) s += (int)data[i];
    return s;
}

/* Ordinary C names inside the precompiled object: these are resolved against
 * host libc once, now.  Rebuilding the caller with the checked layer cannot
 * and does not rewrite these already-compiled references. */
size_t stdc_legacy_copy(unsigned char *dst, const unsigned char *src, size_t n) {
    if (!dst || !src) return 0;
    memcpy(dst, src, n);
    return n;
}

int stdc_legacy_equal(const unsigned char *a, const unsigned char *b, size_t n) {
    return memcmp(a, b, n) == 0;
}
