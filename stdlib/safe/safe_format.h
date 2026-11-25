#ifndef CPLUS_SAFE_FORMAT_H
#define CPLUS_SAFE_FORMAT_H
#include <stddef.h>
#include <stdio.h>
#include <stdarg.h>
#if defined(__cplusplus)
extern "C" {
#endif

/*
 * Bounded, checked formatting adapters with compile-time format-attribute
 * verification and runtime defense-in-depth.
 *
 * Security & error contracts:
 *   - Format string safety (CWE-134): unprefixed `printf_s`, `fprintf_s`,
 *     `sprintf_s`, `snprintf_s`, `scanf_s`, `fscanf_s`, and `sscanf_s` reject
 *     non-literal format strings at runtime (routing through
 *     `cplus_runtime_violation`, exit 70 when fail-stop trap is enabled or
 *     returning -1 / EINVAL when trap is disabled). Full static taint/literal
 *     enforcement is performed by the C+ compiler pass.
 *   - Forbidden specifiers (C11 Annex K K.3.5.3.1): `%n` (including length-
 *     modified forms such as `%hhn`, `%lln`, `%zn`) is refused in all format
 *     and scan functions.
 *   - Scanf width enforcement (CWE-120): `scanf_s`, `fscanf_s`, `sscanf_s`
 *     refuse unbounded `%s` and `%[...]` conversions unless an explicit field
 *     width (e.g. `%63s`) or assignment suppression (`%*s`) is present.
 *   - Destination capacity & UAF: `snprintf_s` / `sprintf_s` check `dest`
 *     against tracked `malloc_s` bounds and `__builtin_object_size(dest, 1)`
 *     (nearest enclosing object, so struct members use their own extent) to
 *     detect overflows where `dmax` exceeds the real buffer capacity, as
 *     well as use-after-free pointers on `dest`, `fmt`, and `str`.
 *   - Return contract (these return `int`, NOT `errno_t` like mem/str):
 *       - return >= 0 : success (C99 snprintf would-be length or scanf count).
 *       - return < 0  : error (`errno = EINVAL` on invalid args/violations).
 */
#if defined(__GNUC__) || defined(__clang__)
#define CPLUS_SAFE_FMT_PRINTF(fmt_idx, arg_idx) __attribute__((format(printf, fmt_idx, arg_idx)))
#define CPLUS_SAFE_FMT_SCANF(fmt_idx, arg_idx)  __attribute__((format(scanf, fmt_idx, arg_idx)))
#define CPLUS_SAFE_IS_LITERAL(s) (__builtin_constant_p(s) ? 1 : 0)
#else
#define CPLUS_SAFE_FMT_PRINTF(fmt_idx, arg_idx)
#define CPLUS_SAFE_FMT_SCANF(fmt_idx, arg_idx)
#define CPLUS_SAFE_IS_LITERAL(s) 1
#endif

#ifndef CPLUS_SAFE_OBJ_SIZE
#if defined(__GNUC__) || defined(__clang__)
/* Type 1 = nearest enclosing object.  Type 0 returns the OBJECT size of the
 * whole enclosing struct for a member pointer (redteam F2: s.a[8] inside a
 * 16-byte struct reported 16), which let dmax exceed the member's real extent
 * and overwrite the adjacent field.  With type 1 the compiler uses the member
 * subobject's true size, so struct members are checked just like flat arrays.
 * For flat arrays and whole objects types 0 and 1 are identical, so valid
 * non-member behavior is unchanged. */
#define CPLUS_SAFE_OBJ_SIZE(p) __builtin_object_size((p), 1)
#else
#define CPLUS_SAFE_OBJ_SIZE(p) ((size_t)-1)
#endif
#endif

#define CPLUS_SAFE_FMT_1ST_(a, ...) (a)
#define CPLUS_SAFE_FMT_1ST(...) CPLUS_SAFE_FMT_1ST_(__VA_ARGS__, 0)
#define CPLUS_SAFE_FMT_2ND_(a, b, ...) (b)
#define CPLUS_SAFE_FMT_2ND(...) CPLUS_SAFE_FMT_2ND_(__VA_ARGS__, 0, 0)
#define CPLUS_SAFE_FMT_3RD_(a, b, c, ...) (c)
#define CPLUS_SAFE_FMT_3RD(...) CPLUS_SAFE_FMT_3RD_(__VA_ARGS__, 0, 0, 0)

int cplus_snprintf_s(char *dest, size_t dmax, const char *fmt, ...) CPLUS_SAFE_FMT_PRINTF(3, 4);
int cplus_vsnprintf_s(char *dest, size_t dmax, const char *fmt, va_list ap) CPLUS_SAFE_FMT_PRINTF(3, 0);
int cplus_printf_s(const char *fmt, ...) CPLUS_SAFE_FMT_PRINTF(1, 2);
int cplus_fprintf_s(FILE *stream, const char *fmt, ...) CPLUS_SAFE_FMT_PRINTF(2, 3);
int cplus_scanf_s(const char *fmt, ...) CPLUS_SAFE_FMT_SCANF(1, 2);
int cplus_fscanf_s(FILE *stream, const char *fmt, ...) CPLUS_SAFE_FMT_SCANF(2, 3);
int cplus_sscanf_s(const char *str, const char *fmt, ...) CPLUS_SAFE_FMT_SCANF(2, 3);
int cplus_sprintf_s(char *dest, size_t dmax, const char *fmt, ...) CPLUS_SAFE_FMT_PRINTF(3, 4);
int cplus_vsprintf_s(char *dest, size_t dmax, const char *fmt, va_list ap) CPLUS_SAFE_FMT_PRINTF(3, 0);

int cplus_printf_s_checked(int fmt_is_literal, const char *fmt, ...) CPLUS_SAFE_FMT_PRINTF(2, 3);
int cplus_fprintf_s_checked(int fmt_is_literal, FILE *stream, const char *fmt, ...) CPLUS_SAFE_FMT_PRINTF(3, 4);
int cplus_snprintf_s_checked(int fmt_is_literal, size_t known_dmax, char *dest, size_t dmax, const char *fmt, ...) CPLUS_SAFE_FMT_PRINTF(5, 6);
int cplus_sprintf_s_checked(int fmt_is_literal, size_t known_dmax, char *dest, size_t dmax, const char *fmt, ...) CPLUS_SAFE_FMT_PRINTF(5, 6);
int cplus_scanf_s_checked(int fmt_is_literal, const char *fmt, ...) CPLUS_SAFE_FMT_SCANF(2, 3);
int cplus_fscanf_s_checked(int fmt_is_literal, FILE *stream, const char *fmt, ...) CPLUS_SAFE_FMT_SCANF(3, 4);
int cplus_sscanf_s_checked(int fmt_is_literal, const char *str, const char *fmt, ...) CPLUS_SAFE_FMT_SCANF(3, 4);

#ifndef CPLUS_SAFE_NO_UNPREFIXED_MACROS
#undef printf_s
#define printf_s(...) \
    cplus_printf_s_checked(CPLUS_SAFE_IS_LITERAL(CPLUS_SAFE_FMT_1ST(__VA_ARGS__)), __VA_ARGS__)
#undef fprintf_s
#define fprintf_s(...) \
    cplus_fprintf_s_checked(CPLUS_SAFE_IS_LITERAL(CPLUS_SAFE_FMT_2ND(__VA_ARGS__)), __VA_ARGS__)
#undef snprintf_s
#define snprintf_s(...) \
    cplus_snprintf_s_checked(CPLUS_SAFE_IS_LITERAL(CPLUS_SAFE_FMT_3RD(__VA_ARGS__)), CPLUS_SAFE_OBJ_SIZE(CPLUS_SAFE_FMT_1ST(__VA_ARGS__)), __VA_ARGS__)
#undef sprintf_s
#define sprintf_s(...) \
    cplus_sprintf_s_checked(CPLUS_SAFE_IS_LITERAL(CPLUS_SAFE_FMT_3RD(__VA_ARGS__)), CPLUS_SAFE_OBJ_SIZE(CPLUS_SAFE_FMT_1ST(__VA_ARGS__)), __VA_ARGS__)
#undef scanf_s
#define scanf_s(...) \
    cplus_scanf_s_checked(CPLUS_SAFE_IS_LITERAL(CPLUS_SAFE_FMT_1ST(__VA_ARGS__)), __VA_ARGS__)
#undef fscanf_s
#define fscanf_s(...) \
    cplus_fscanf_s_checked(CPLUS_SAFE_IS_LITERAL(CPLUS_SAFE_FMT_2ND(__VA_ARGS__)), __VA_ARGS__)
#undef sscanf_s
#define sscanf_s(...) \
    cplus_sscanf_s_checked(CPLUS_SAFE_IS_LITERAL(CPLUS_SAFE_FMT_2ND(__VA_ARGS__)), __VA_ARGS__)
#undef vsnprintf_s
#define vsnprintf_s cplus_vsnprintf_s
#undef vsprintf_s
#define vsprintf_s cplus_vsprintf_s
#endif
#if defined(__cplusplus)
}
#endif
#endif
