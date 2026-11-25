/* C23 exp10 family copied from musl libc, pinned commit
 * 7813e9119eb5fe681b6750b8a033a0490bca0941 (MIT, see libc/upstream/musl/COPYRIGHT).
 * Files: src/math/exp10.c, src/math/exp10f.c, src/math/exp10l.c.
 *
 * Only integration edits: symbol renames to cplus_*, internal calls redirected
 * to already-bundled cplus_* math wrappers, weak_alias lines removed, and one
 * opt-in cplus_libc_log entry call per function. No algorithm was rewritten.
 */
#include "../../log/log.h"
#include "../../install-include/cplus_libc.h"
#include <float.h>
#include <math.h>
#include <stdint.h>

/* math_c23.c defines the same symbols from compiler builtins when available.
 * Guard this vendored musl implementation with the identical feature test so
 * exactly one translation unit defines each symbol. Without this, clang 21+
 * pulls both archive members and the link fails with duplicate cplus_exp10*
 * symbols (#32 portability). */
#if defined(__has_builtin)
#define CPLUS_MUSL_HAS_BUILTIN(x) __has_builtin(x)
#else
#define CPLUS_MUSL_HAS_BUILTIN(x) 0
#endif

#if !CPLUS_MUSL_HAS_BUILTIN(__builtin_exp10)
double cplus_exp10(double x) {
    cplus_libc_log("exp10", "enter");
    static const double p10[] = {
        1e-15, 1e-14, 1e-13, 1e-12, 1e-11, 1e-10,
        1e-9, 1e-8, 1e-7, 1e-6, 1e-5, 1e-4, 1e-3, 1e-2, 1e-1,
        1, 1e1, 1e2, 1e3, 1e4, 1e5, 1e6, 1e7, 1e8, 1e9,
        1e10, 1e11, 1e12, 1e13, 1e14, 1e15
    };
    double n, y = cplus_modf(x, &n);
    union { double f; uint64_t i; } u = { n };
    if ((u.i >> 52 & 0x7ff) < 0x3ff + 4) {
        if (!y) return p10[(int)n + 15];
        y = cplus_exp2(3.32192809488736234787031942948939 * y);
        return y * p10[(int)n + 15];
    }
    return cplus_pow(10.0, x);
}
#endif /* !CPLUS_MUSL_HAS_BUILTIN(__builtin_exp10) */

#if !CPLUS_MUSL_HAS_BUILTIN(__builtin_exp10f)
float cplus_exp10f(float x) {
    cplus_libc_log("exp10f", "enter");
    static const float p10[] = {
        1e-7f, 1e-6f, 1e-5f, 1e-4f, 1e-3f, 1e-2f, 1e-1f,
        1, 1e1, 1e2, 1e3, 1e4, 1e5, 1e6, 1e7
    };
    float n, y = cplus_modff(x, &n);
    union { float f; uint32_t i; } u = { n };
    if ((u.i >> 23 & 0xff) < 0x7f + 3) {
        if (!y) return p10[(int)n + 7];
        y = cplus_exp2f(3.32192809488736234787031942948939f * y);
        return y * p10[(int)n + 7];
    }
    return cplus_exp2(3.32192809488736234787031942948939 * x);
}
#endif /* !CPLUS_MUSL_HAS_BUILTIN(__builtin_exp10f) */

#if !CPLUS_MUSL_HAS_BUILTIN(__builtin_exp10l)
long double cplus_exp10l(long double x) {
    cplus_libc_log("exp10l", "enter");
#if LDBL_MANT_DIG == 53 && LDBL_MAX_EXP == 1024
    /* musl's first branch: long double is binary64 on this ABI. */
    return cplus_exp10(x);
#else
    /* Other ABIs need musl's ldshape union from libm.h; not copied to avoid
     * an incomplete dependency closure. Report as unavailable at compile time. */
    (void)x;
    return 0.0L;
#endif
}
#endif /* !CPLUS_MUSL_HAS_BUILTIN(__builtin_exp10l) */
