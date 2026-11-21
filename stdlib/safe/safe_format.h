#ifndef CPLUS_SAFE_FORMAT_H
#define CPLUS_SAFE_FORMAT_H
#include <stddef.h>
#include <stdio.h>
#include <stdarg.h>
#if defined(__cplusplus)
extern "C" {
#endif

/*
 * Bounded, checked formatting adapters.
 *
 * Error / return contract (these return `int`, NOT `errno_t` like the mem/str
 * adapters, so the status lives in the return value and in `errno`):
 *   - return >= 0 : success. For the bounded variants this is the number of
 *     bytes that would have been written, excluding the terminating NUL. If the
 *     result is >= dmax the output was TRUNCATED, but `dest` still holds a
 *     valid, NUL-terminated prefix of length at most dmax-1.
 *   - return < 0  : error. `errno` is set to EINVAL for invalid arguments
 *     (null format, or dest==NULL with dmax!=0), otherwise to the value the
 *     underlying C library call reported.
 *
 * Truncation contract (bounded, NUL-terminated, returns would-be length):
 *   the bounded entry points behave like C99 `snprintf`: they never write past
 *   `dmax` bytes and never overflow `dest`; on truncation they report the full
 *   required length so callers can size a retry. `cplus_sprintf_s` and
 *   `cplus_vsprintf_s` are ALIASES of this bounded behavior, NOT Annex K
 *   `sprintf_s` (which rejects on overflow instead of truncating).
 *
 * Scope / honesty: this is bounded formatting, not Rust-grade safety. A caller
 * that passes a `dmax` larger than the real capacity of `dest`, or a format
 * string whose conversions do not match the arguments, is still invoking
 * undefined behavior the library cannot detect.
 */
int cplus_snprintf_s(char *dest, size_t dmax, const char *fmt, ...);
int cplus_vsnprintf_s(char *dest, size_t dmax, const char *fmt, va_list ap);
int cplus_printf_s(const char *fmt, ...);
int cplus_fprintf_s(FILE *stream, const char *fmt, ...);
int cplus_scanf_s(const char *fmt, ...);
int cplus_fscanf_s(FILE *stream, const char *fmt, ...);
int cplus_sprintf_s(char *dest, size_t dmax, const char *fmt, ...);
int cplus_vsprintf_s(char *dest, size_t dmax, const char *fmt, va_list ap);
#if defined(__cplusplus)
}
#endif
#endif
