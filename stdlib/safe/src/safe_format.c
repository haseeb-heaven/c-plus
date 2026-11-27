#include "../safe_format.h"
#include "../safe_alloc.h"
#include "../safe_io.h"
#include "../safe_log.h"
#include "../safe_runtime.h"
#include "../safe_stats.h"
#include <errno.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/*
 * Formatting contract (documented in full in safe_format.h):
 *   - Bounded variants (cplus_snprintf_s / cplus_vsnprintf_s / cplus_sprintf_s /
 *     cplus_vsprintf_s) never write past `dmax` bytes and always NUL-terminate
 *     when dmax > 0, and refuse `dmax` exceeding known heap/object capacity.
 *   - Unprefixed *_s macros pass compile-time `__builtin_constant_p(fmt)` to
 *     refuse attacker-controlled non-literal format strings (CWE-134).
 *   - `%n` is forbidden in all printf and scanf variants (Annex K K.3.5.3.1).
 *   - `scanf_s`, `fscanf_s`, and `sscanf_s` refuse unbounded `%s` and `%[...]`.
 *   - Logging is metadata-only: TRACE on entry, ERROR/violation on errors
 *     (WARN for the scanf family, where a negative result may just be EOF).
 */

/* Logs one ERROR line and normalizes errno (keeps a more specific libc value). */
static int fmt_error(const char *api, const char *msg) {
    if (errno == 0) errno = EINVAL;
    cplus_safe_log(CPLUS_SAFE_LOG_ERROR, api, msg);
    return -1;
}

static int fmt_violation(const char *api, const char *msg) {
    errno = EINVAL;
    cplus_safe_stat_record(api, 0);
    cplus_runtime_violation(api, msg);
    return -1;
}

/* Defense-in-depth: a FILE* handed to fprintf_s/fscanf_s may have already been
 * closed through cplus_fclose_s.  Calling vfprintf/vfscanf on it is a
 * use-after-free even if the caller's pointer is non-NULL, so refuse it before
 * any host I/O.  Streams never seen by the checked I/O layer report 0 and keep
 * their normal behavior (raw-host streams remain supported). */
static int fmt_check_stream(const char *api, FILE *stream) {
    if (!stream) return 0; /* the existing NULL-stream path owns this */
    if (cplus_io_stream_is_closed(stream))
        return fmt_violation(api, "checked stream was closed (use-after-close)");
    return 0;
}

/* Parse optional N$ positional argument index; returns 1 if consumed & valid
 * (1..32), -1 if malformed/out-of-range (%$ / %0$ / >32), 0 if no N$. */
static int parse_pos_dollar(const unsigned char **pp) {
    const unsigned char *q = *pp;
    unsigned int val = 0;
    if (*q == '$') return -1;
    while (*q >= '0' && *q <= '9') {
        val = val < 100u ? (val * 10u + (unsigned int)(*q - '0')) : 100u;
        ++q;
    }
    if (q > *pp && *q == '$') {
        if (val == 0u || val > 32u) return -1;
        *pp = q + 1;
        return 1;
    }
    return 0;
}

/* Skip a run of flags/length modifiers, including the glibc 'I' and 'm'
 * flags and the C23 exact-width wN / wfN modifiers (w8/w16/w32/w64 and
 * wf8..wf64).  Older filters stopped at 'I'/'w'/'m' and missed a following
 * 'n', allowing %In, %w32n and %mn to reach the host printf.  A bare %m is
 * the glibc strerror conversion and is left alone. */
static const unsigned char *skip_fmt_mods(const unsigned char *p) {
    for (;;) {
        while (*p == '-' || *p == '+' || *p == ' ' || *p == '#' || *p == '0' ||
               *p == '\'' || *p == 'I' ||
               (*p == 'm' && p[1] != '\0' && p[1] != '%' && p[1] != '$')) ++p;
        if (*p == 'w') {
            const unsigned char *q = p + 1;
            if (*q == 'f') ++q;
            if (*q == '8' || (q[0] == '1' && q[1] == '6') ||
                (q[0] == '3' && q[1] == '2') ||
                (q[0] == '6' && q[1] == '4')) {
                p = q + ((*q == '8') ? 1 : 2);
                continue;
            }
        }
        if (*p == 'h' || *p == 'l' || *p == 'j' || *p == 'z' || *p == 't' ||
            *p == 'L' || *p == 'q') { ++p; continue; }
        return p;
    }
}

/* Refuse any '$' anywhere: libc accepts positional markers in many positions
 * (even after a length modifier, e.g. %h1$n), so every missed placement could
 * turn a later 'n' into a write.  This strict policy also refuses literal '$'
 * text, which is acceptable for these checked APIs. */
static int fmt_has_positional(const char *fmt) {
    const unsigned char *s = (const unsigned char *)fmt;
    while (*s) {
        if (*s == '$') return 1;
        ++s;
    }
    return 0;
}

static int cplus_fmt_has_percent_n(const char *fmt) {
    const unsigned char *p = (const unsigned char *)fmt;
    if (!p) return 0;
    if (fmt_has_positional(fmt)) return 1;
    while (*p) {
        int pr;
        if (*p != '%') { ++p; continue; }
        ++p;
        if (*p == '%') { ++p; continue; }
        pr = parse_pos_dollar(&p);
        if (pr != 0) return 1;
        p = skip_fmt_mods(p);
        if (*p == '*') {
            ++p;
            if (parse_pos_dollar(&p) != 0) return 1;
        } else {
            while (*p >= '0' && *p <= '9') ++p;
        }
        if (*p == '.') {
            ++p;
            if (*p == '*') {
                ++p;
                if (parse_pos_dollar(&p) != 0) return 1;
            } else {
                while (*p >= '0' && *p <= '9') ++p;
            }
        }
        if (*p == '$') return 1; /* repeated or misplaced positional marker */
        p = skip_fmt_mods(p);    /* length modifiers, incl. 'I', wN / wfN */
        if (*p == '$' || *p == 'n') return 1;
        if (*p) ++p;
    }
    return 0;
}

/* Returns 0 if safe, 1 if %n / positional / allocating conversion present,
 * 2 if an unbounded wide/narrow string conversion is present. */
static int cplus_scanf_fmt_check(const char *fmt) {
    const unsigned char *p = (const unsigned char *)fmt;
    if (!p) return 0;
    if (fmt_has_positional(fmt)) return 1;
    while (*p) {
        int suppressed = 0;
        int has_width = 0;
        int pr;
        if (*p != '%') { ++p; continue; }
        ++p;
        if (*p == '%') { ++p; continue; }
        p = skip_fmt_mods(p);
        if (*p == '*') { suppressed = 1; ++p; }
        pr = parse_pos_dollar(&p);
        if (pr != 0) return 1;
        if (*p == '*') { suppressed = 1; ++p; }
        while (*p >= '0' && *p <= '9') {
            if (*p >= '1') has_width = 1;
            ++p;
        }
        if (*p == '$') return 1;
        p = skip_fmt_mods(p);
        if (*p == '$' || *p == 'n') return 1;
        /* glibc 'm' allocates an untracked host buffer; POSIX %S reads an
         * unbounded wide string.  Neither is permitted by these checked APIs. */
        if (*p == 'm') return 1;
        if (*p == 'S') return 2;
        if ((*p == 's' || *p == '[') && !suppressed && !has_width) return 2;
        if (*p == '[') {
            ++p;
            if (*p == '^') ++p;
            if (*p == ']') ++p;
            while (*p && *p != ']') ++p;
            if (*p == ']') ++p;
            continue;
        }
        if (*p) ++p;
    }
    return 0;
}

static int fmt_check_dest_bounds(const char *api, const char *dest, size_t dmax, size_t known_dmax) {
    if (!dest) return 0;
    if (cplus_safe_ptr_was_freed(dest)) {
        return fmt_violation(api, "use-after-free detected in format destination");
    }
    {
        size_t tracked = cplus_safe_ptr_size(dest);
        size_t remaining = cplus_safe_remaining_capacity(dest);
        /* (size_t)-1 is the "not in a tracked live block" sentinel, never a
         * capacity: treating it as one (e.g. `remaining > 0 ? remaining : ...`)
         * makes cap == SIZE_MAX for every stack/static destination and silently
         * disables known_dmax bounds checking.  Use remaining bytes for tracked
         * destinations -- including a genuine zero-byte one-past-the-end
         * pointer -- otherwise fall back to the base size / caller's known
         * dmax, and skip the check only for a truly unknown capacity. */
        size_t cap = (size_t)-1;
        if (remaining != (size_t)-1) cap = remaining;
        else if (tracked > 0) cap = tracked;
        else if (known_dmax != (size_t)-1) cap = known_dmax;
        if (cap != (size_t)-1 && dmax > cap) {
            return fmt_violation(api, "format dmax exceeds destination buffer capacity");
        }
    }
    return 0;
}

static int fmt_check_scanf_spec(const char *api, const char *fmt) {
    int issue = cplus_scanf_fmt_check(fmt);
    if (issue == 1) return fmt_violation(api, "forbidden %n, positional or allocating specifier in scanf");
    if (issue == 2) return fmt_violation(api, "unbounded %s, %S or %[ specifier in scanf without field width");
    return 0;
}

int cplus_vsnprintf_s(char *dest, size_t dmax, const char *fmt, va_list ap) {
    cplus_safe_log(CPLUS_SAFE_LOG_TRACE, "vsnprintf_s", "enter");
    if (!fmt) { errno = EINVAL; cplus_safe_log(CPLUS_SAFE_LOG_ERROR, "vsnprintf_s", "null format"); cplus_safe_stat_record("vsnprintf_s", 0); return -1; }
    if (cplus_safe_ptr_was_freed(fmt)) return fmt_violation("vsnprintf_s", "use-after-free detected in format string");
    if (!dest && dmax != 0) { errno = EINVAL; cplus_safe_log(CPLUS_SAFE_LOG_ERROR, "vsnprintf_s", "invalid dest/dmax"); cplus_safe_stat_record("vsnprintf_s", 0); return -1; }
    if (fmt_check_dest_bounds("vsnprintf_s", dest, dmax, (size_t)-1) < 0) return -1;
    if (cplus_fmt_has_percent_n(fmt)) return fmt_violation("vsnprintf_s", "forbidden %n specifier in format string");
    {
        int n = vsnprintf(dest, dmax, fmt, ap);
        if (n < 0) { cplus_safe_stat_record("vsnprintf_s", 0); return fmt_error("vsnprintf_s", "format error"); }
        cplus_safe_stat_record("vsnprintf_s", 1);
        return n;
    }
}
int cplus_snprintf_s(char *dest, size_t dmax, const char *fmt, ...) {
    cplus_safe_log(CPLUS_SAFE_LOG_TRACE, "snprintf_s", "enter");
    va_list ap; va_start(ap, fmt);
    int n = cplus_vsnprintf_s(dest, dmax, fmt, ap);
    va_end(ap);
    cplus_safe_stat_record("snprintf_s", n >= 0);
    return n;
}
int cplus_snprintf_s_checked(int fmt_is_literal, size_t known_dmax, char *dest, size_t dmax, const char *fmt, ...) {
    cplus_safe_log(CPLUS_SAFE_LOG_TRACE, "snprintf_s", "enter");
    if (!fmt) { errno = EINVAL; cplus_safe_log(CPLUS_SAFE_LOG_ERROR, "snprintf_s", "null format"); cplus_safe_stat_record("snprintf_s", 0); return -1; }
    if (cplus_safe_ptr_was_freed(fmt)) return fmt_violation("snprintf_s", "use-after-free detected in format string");
    if (!fmt_is_literal) return fmt_violation("snprintf_s", "non-literal format string refused (CWE-134)");
    if (!dest && dmax != 0) { errno = EINVAL; cplus_safe_log(CPLUS_SAFE_LOG_ERROR, "snprintf_s", "invalid dest/dmax"); cplus_safe_stat_record("snprintf_s", 0); return -1; }
    if (fmt_check_dest_bounds("snprintf_s", dest, dmax, known_dmax) < 0) return -1;
    {
        va_list ap; va_start(ap, fmt);
        int n = cplus_vsnprintf_s(dest, dmax, fmt, ap);
        va_end(ap);
        cplus_safe_stat_record("snprintf_s", n >= 0);
        return n;
    }
}
int cplus_printf_s(const char *fmt, ...) {
    cplus_safe_log(CPLUS_SAFE_LOG_TRACE, "printf_s", "enter");
    if (!fmt) { errno = EINVAL; cplus_safe_log(CPLUS_SAFE_LOG_ERROR, "printf_s", "null format"); cplus_safe_stat_record("printf_s", 0); return -1; }
    if (cplus_safe_ptr_was_freed(fmt)) return fmt_violation("printf_s", "use-after-free detected in format string");
    if (cplus_fmt_has_percent_n(fmt)) return fmt_violation("printf_s", "forbidden %n specifier in format string");
    {
        va_list ap; va_start(ap, fmt);
        int n = vprintf(fmt, ap);
        va_end(ap);
        if (n < 0) { cplus_safe_stat_record("printf_s", 0); return fmt_error("printf_s", "io/format error"); }
        cplus_safe_stat_record("printf_s", 1);
        return n;
    }
}
int cplus_printf_s_checked(int fmt_is_literal, const char *fmt, ...) {
    cplus_safe_log(CPLUS_SAFE_LOG_TRACE, "printf_s", "enter");
    if (!fmt) { errno = EINVAL; cplus_safe_log(CPLUS_SAFE_LOG_ERROR, "printf_s", "null format"); cplus_safe_stat_record("printf_s", 0); return -1; }
    if (cplus_safe_ptr_was_freed(fmt)) return fmt_violation("printf_s", "use-after-free detected in format string");
    if (!fmt_is_literal) return fmt_violation("printf_s", "non-literal format string refused (CWE-134)");
    if (cplus_fmt_has_percent_n(fmt)) return fmt_violation("printf_s", "forbidden %n specifier in format string");
    {
        va_list ap; va_start(ap, fmt);
        int n = vprintf(fmt, ap);
        va_end(ap);
        if (n < 0) { cplus_safe_stat_record("printf_s", 0); return fmt_error("printf_s", "io/format error"); }
        cplus_safe_stat_record("printf_s", 1);
        return n;
    }
}
int cplus_fprintf_s(FILE *stream, const char *fmt, ...) {
    cplus_safe_log(CPLUS_SAFE_LOG_TRACE, "fprintf_s", "enter");
    if (!stream) { errno = EINVAL; cplus_safe_log(CPLUS_SAFE_LOG_ERROR, "fprintf_s", "null stream"); cplus_safe_stat_record("fprintf_s", 0); return -1; }
    if (!fmt) { errno = EINVAL; cplus_safe_log(CPLUS_SAFE_LOG_ERROR, "fprintf_s", "null format"); cplus_safe_stat_record("fprintf_s", 0); return -1; }
    if (fmt_check_stream("fprintf_s", stream) < 0) return -1;
    if (cplus_safe_ptr_was_freed(fmt)) return fmt_violation("fprintf_s", "use-after-free detected in format string");
    if (cplus_fmt_has_percent_n(fmt)) return fmt_violation("fprintf_s", "forbidden %n specifier in format string");
    {
        va_list ap; va_start(ap, fmt);
        int n = vfprintf(stream, fmt, ap);
        va_end(ap);
        if (n < 0) { cplus_safe_stat_record("fprintf_s", 0); return fmt_error("fprintf_s", "io/format error"); }
        cplus_safe_stat_record("fprintf_s", 1);
        return n;
    }
}
int cplus_fprintf_s_checked(int fmt_is_literal, FILE *stream, const char *fmt, ...) {
    cplus_safe_log(CPLUS_SAFE_LOG_TRACE, "fprintf_s", "enter");
    if (!stream) { errno = EINVAL; cplus_safe_log(CPLUS_SAFE_LOG_ERROR, "fprintf_s", "null stream"); cplus_safe_stat_record("fprintf_s", 0); return -1; }
    if (!fmt) { errno = EINVAL; cplus_safe_log(CPLUS_SAFE_LOG_ERROR, "fprintf_s", "null format"); cplus_safe_stat_record("fprintf_s", 0); return -1; }
    if (fmt_check_stream("fprintf_s", stream) < 0) return -1;
    if (cplus_safe_ptr_was_freed(fmt)) return fmt_violation("fprintf_s", "use-after-free detected in format string");
    if (!fmt_is_literal) return fmt_violation("fprintf_s", "non-literal format string refused (CWE-134)");
    if (cplus_fmt_has_percent_n(fmt)) return fmt_violation("fprintf_s", "forbidden %n specifier in format string");
    {
        va_list ap; va_start(ap, fmt);
        int n = vfprintf(stream, fmt, ap);
        va_end(ap);
        if (n < 0) { cplus_safe_stat_record("fprintf_s", 0); return fmt_error("fprintf_s", "io/format error"); }
        cplus_safe_stat_record("fprintf_s", 1);
        return n;
    }
}
/* ---- Annex K / POSIX scanf argument-shape dispatcher (issue #192) -------
 *
 * The exported scanf_s family used to forward its va_list straight to host
 * vscanf/vfscanf/vsscanf.  Annex K callers pass an extra rsize_t capacity after
 * every %s/%c/%[/%S/%C pointer; the host does not consume it, so the following
 * conversion reads the capacity as a pointer (e.g. 16 -> address 0x10, SIGSEGV).
 * Inspect the argument words once, drop any Annex K capacities, and forward
 * only the real pointer arguments.  POSIX-shaped calls keep the historical
 * zero-copy vscanf path.  All scanf conversion arguments are pointer-sized on
 * the supported ABIs; the word after a bounded pointer is only read when a
 * later conversion guarantees another argument exists. */
#define CPLUS_SCAN_MAX_ARGS 16u
#if UINTPTR_MAX > 0xffffffffu
#define CPLUS_SCAN_CAP_MAX ((uintptr_t)0x1000000u)
#else
#define CPLUS_SCAN_CAP_MAX ((uintptr_t)0x10000u)
#endif

/* Host scanf ignores surplus variadic arguments, so always pass a full,
 * NULL-padded pointer vector instead of 16 hand-written forwarding shims. */
static int scan_call_host(int mode, FILE *stream, const char *str,
                          const char *fmt, void *const *args) {
    if (mode == 1)
        return fscanf(stream, fmt, args[0], args[1], args[2], args[3], args[4],
                      args[5], args[6], args[7], args[8], args[9], args[10],
                      args[11], args[12], args[13], args[14], args[15]);
    if (mode == 2)
        return sscanf(str, fmt, args[0], args[1], args[2], args[3], args[4],
                      args[5], args[6], args[7], args[8], args[9], args[10],
                      args[11], args[12], args[13], args[14], args[15]);
    return scanf(fmt, args[0], args[1], args[2], args[3], args[4], args[5],
                 args[6], args[7], args[8], args[9], args[10], args[11],
                 args[12], args[13], args[14], args[15]);
}

/* mode: 0 = vscanf, 1 = vfscanf, 2 = vsscanf.  Unbounded %s/%[/%S and %n were
 * already refused by fmt_check_scanf_spec(), so only sized bounded conversions
 * reach here. */
static int scan_dispatch(const char *api, int mode, FILE *stream, const char *str,
                         const char *fmt, va_list ap) {
    const unsigned char *p = (const unsigned char *)fmt;
    int bounded[CPLUS_SCAN_MAX_ARGS];
    size_t width[CPLUS_SCAN_MAX_ARGS];
    void *args[CPLUS_SCAN_MAX_ARGS];
    size_t nconv = 0, i;
    void *pending = NULL;
    int have_pending = 0, annexk = 0;
    va_list peek;

    while (*p) {
        int suppressed = 0;
        size_t w = 0;
        unsigned char conv;
        if (*p != '%') { ++p; continue; }
        ++p;
        if (*p == '%') { ++p; continue; }
        p = skip_fmt_mods(p);
        if (*p == '*') { suppressed = 1; ++p; }
        (void)parse_pos_dollar(&p);
        if (*p == '*') { suppressed = 1; ++p; }
        while (*p >= '0' && *p <= '9') {
            if (w < 1000000u) w = w * 10u + (size_t)(*p - '0');
            ++p;
        }
        p = skip_fmt_mods(p);
        conv = *p;
        if (conv == '[') {
            ++p;
            if (*p == '^') ++p;
            if (*p == ']') ++p;
            while (*p && *p != ']') ++p;
            if (*p == ']') ++p;
        } else if (conv) {
            ++p;
        }
        if (!suppressed && conv) {
            if (nconv == CPLUS_SCAN_MAX_ARGS)
                return fmt_violation(api, "too many scanf conversions");
            bounded[nconv] = (conv == 's' || conv == 'c' || conv == '[' ||
                              conv == 'S' || conv == 'C');
            width[nconv] = w;
            ++nconv;
        }
    }

    memset(args, 0, sizeof args);
    va_copy(peek, ap);
    for (i = 0; i < nconv; ++i) {
        void *word;
        if (have_pending) { word = pending; have_pending = 0; }
        else word = va_arg(peek, void *);
        args[i] = word;
        /* A capacity can only desynchronise the host if another conversion
         * follows; that guarantees the next argument word really exists. */
        if (bounded[i] && i + 1u < nconv) {
            void *next = va_arg(peek, void *);
            uintptr_t v = (uintptr_t)next;
            if (v != 0u && v <= CPLUS_SCAN_CAP_MAX) {
                annexk = 1;
                /* Annex K would clamp to capacity-1; the host honours only
                 * the explicit width.  Refuse a capacity too small for the
                 * declared field rather than overrun the destination. */
                if (width[i] != 0u && (size_t)v <= width[i]) {
                    va_end(peek);
                    return fmt_violation(api, "scanf capacity smaller than field width");
                }
            } else {
                pending = next;
                have_pending = 1;
            }
        }
    }
    va_end(peek);

    if (!annexk) {
        if (mode == 1) return vfscanf(stream, fmt, ap);
        if (mode == 2) return vsscanf(str, fmt, ap);
        return vscanf(fmt, ap);
    }
    return scan_call_host(mode, stream, str, fmt, args);
}

int cplus_scanf_s(const char *fmt, ...) {
    cplus_safe_log(CPLUS_SAFE_LOG_TRACE, "scanf_s", "enter");
    if (!fmt) { errno = EINVAL; cplus_safe_log(CPLUS_SAFE_LOG_ERROR, "scanf_s", "null format"); cplus_safe_stat_record("scanf_s", 0); return -1; }
    if (cplus_safe_ptr_was_freed(fmt)) return fmt_violation("scanf_s", "use-after-free detected in format string");
    if (fmt_check_scanf_spec("scanf_s", fmt) < 0) return -1;
    {
        va_list ap; va_start(ap, fmt);
        int n = scan_dispatch("scanf_s", 0, NULL, NULL, fmt, ap);
        va_end(ap);
        if (n < 0) cplus_safe_log(CPLUS_SAFE_LOG_WARN, "scanf_s", "io/format error or EOF");
        cplus_safe_stat_record("scanf_s", n >= 0);
        return n;
    }
}
int cplus_scanf_s_checked(int fmt_is_literal, const char *fmt, ...) {
    cplus_safe_log(CPLUS_SAFE_LOG_TRACE, "scanf_s", "enter");
    if (!fmt) { errno = EINVAL; cplus_safe_log(CPLUS_SAFE_LOG_ERROR, "scanf_s", "null format"); cplus_safe_stat_record("scanf_s", 0); return -1; }
    if (cplus_safe_ptr_was_freed(fmt)) return fmt_violation("scanf_s", "use-after-free detected in format string");
    if (!fmt_is_literal) return fmt_violation("scanf_s", "non-literal format string refused (CWE-134)");
    if (fmt_check_scanf_spec("scanf_s", fmt) < 0) return -1;
    {
        va_list ap; va_start(ap, fmt);
        int n = scan_dispatch("scanf_s", 0, NULL, NULL, fmt, ap);
        va_end(ap);
        if (n < 0) cplus_safe_log(CPLUS_SAFE_LOG_WARN, "scanf_s", "io/format error or EOF");
        cplus_safe_stat_record("scanf_s", n >= 0);
        return n;
    }
}
int cplus_fscanf_s(FILE *stream, const char *fmt, ...) {
    cplus_safe_log(CPLUS_SAFE_LOG_TRACE, "fscanf_s", "enter");
    if (!stream) { errno = EINVAL; cplus_safe_log(CPLUS_SAFE_LOG_ERROR, "fscanf_s", "null stream"); cplus_safe_stat_record("fscanf_s", 0); return -1; }
    if (!fmt) { errno = EINVAL; cplus_safe_log(CPLUS_SAFE_LOG_ERROR, "fscanf_s", "null format"); cplus_safe_stat_record("fscanf_s", 0); return -1; }
    if (fmt_check_stream("fscanf_s", stream) < 0) return -1;
    if (cplus_safe_ptr_was_freed(fmt)) return fmt_violation("fscanf_s", "use-after-free detected in format string");
    if (fmt_check_scanf_spec("fscanf_s", fmt) < 0) return -1;
    {
        va_list ap; va_start(ap, fmt);
        int n = scan_dispatch("fscanf_s", 1, stream, NULL, fmt, ap);
        va_end(ap);
        if (n < 0) cplus_safe_log(CPLUS_SAFE_LOG_WARN, "fscanf_s", "io/format error or EOF");
        cplus_safe_stat_record("fscanf_s", n >= 0);
        return n;
    }
}
int cplus_fscanf_s_checked(int fmt_is_literal, FILE *stream, const char *fmt, ...) {
    cplus_safe_log(CPLUS_SAFE_LOG_TRACE, "fscanf_s", "enter");
    if (!stream) { errno = EINVAL; cplus_safe_log(CPLUS_SAFE_LOG_ERROR, "fscanf_s", "null stream"); cplus_safe_stat_record("fscanf_s", 0); return -1; }
    if (!fmt) { errno = EINVAL; cplus_safe_log(CPLUS_SAFE_LOG_ERROR, "fscanf_s", "null format"); cplus_safe_stat_record("fscanf_s", 0); return -1; }
    if (fmt_check_stream("fscanf_s", stream) < 0) return -1;
    if (cplus_safe_ptr_was_freed(fmt)) return fmt_violation("fscanf_s", "use-after-free detected in format string");
    if (!fmt_is_literal) return fmt_violation("fscanf_s", "non-literal format string refused (CWE-134)");
    if (fmt_check_scanf_spec("fscanf_s", fmt) < 0) return -1;
    {
        va_list ap; va_start(ap, fmt);
        int n = scan_dispatch("fscanf_s", 1, stream, NULL, fmt, ap);
        va_end(ap);
        if (n < 0) cplus_safe_log(CPLUS_SAFE_LOG_WARN, "fscanf_s", "io/format error or EOF");
        cplus_safe_stat_record("fscanf_s", n >= 0);
        return n;
    }
}
int cplus_sscanf_s(const char *str, const char *fmt, ...) {
    cplus_safe_log(CPLUS_SAFE_LOG_TRACE, "sscanf_s", "enter");
    if (!str || !fmt) { errno = EINVAL; cplus_safe_log(CPLUS_SAFE_LOG_ERROR, "sscanf_s", "null input/format"); cplus_safe_stat_record("sscanf_s", 0); return -1; }
    if (cplus_safe_ptr_was_freed(str)) return fmt_violation("sscanf_s", "use-after-free detected in input string");
    if (cplus_safe_ptr_was_freed(fmt)) return fmt_violation("sscanf_s", "use-after-free detected in format string");
    if (fmt_check_scanf_spec("sscanf_s", fmt) < 0) return -1;
    {
        va_list ap; va_start(ap, fmt);
        int n = scan_dispatch("sscanf_s", 2, NULL, str, fmt, ap);
        va_end(ap);
        if (n < 0) cplus_safe_log(CPLUS_SAFE_LOG_WARN, "sscanf_s", "input/format error or EOF");
        cplus_safe_stat_record("sscanf_s", n >= 0);
        return n;
    }
}
int cplus_sscanf_s_checked(int fmt_is_literal, const char *str, const char *fmt, ...) {
    cplus_safe_log(CPLUS_SAFE_LOG_TRACE, "sscanf_s", "enter");
    if (!str || !fmt) { errno = EINVAL; cplus_safe_log(CPLUS_SAFE_LOG_ERROR, "sscanf_s", "null input/format"); cplus_safe_stat_record("sscanf_s", 0); return -1; }
    if (cplus_safe_ptr_was_freed(str)) return fmt_violation("sscanf_s", "use-after-free detected in input string");
    if (cplus_safe_ptr_was_freed(fmt)) return fmt_violation("sscanf_s", "use-after-free detected in format string");
    if (!fmt_is_literal) return fmt_violation("sscanf_s", "non-literal format string refused (CWE-134)");
    if (fmt_check_scanf_spec("sscanf_s", fmt) < 0) return -1;
    {
        va_list ap; va_start(ap, fmt);
        int n = scan_dispatch("sscanf_s", 2, NULL, str, fmt, ap);
        va_end(ap);
        if (n < 0) cplus_safe_log(CPLUS_SAFE_LOG_WARN, "sscanf_s", "input/format error or EOF");
        cplus_safe_stat_record("sscanf_s", n >= 0);
        return n;
    }
}
int cplus_vsprintf_s(char *dest, size_t dmax, const char *fmt, va_list ap) {
    cplus_safe_log(CPLUS_SAFE_LOG_TRACE, "vsprintf_s", "enter");
    /* Alias of the bounded vsnprintf behavior (see header): NOT Annex K. */
    int n = cplus_vsnprintf_s(dest, dmax, fmt, ap);
    cplus_safe_stat_record("vsprintf_s", n >= 0);
    return n;
}
int cplus_sprintf_s(char *dest, size_t dmax, const char *fmt, ...) {
    cplus_safe_log(CPLUS_SAFE_LOG_TRACE, "sprintf_s", "enter");
    va_list ap; va_start(ap, fmt);
    int n = cplus_vsprintf_s(dest, dmax, fmt, ap);
    va_end(ap);
    cplus_safe_stat_record("sprintf_s", n >= 0);
    return n;
}
int cplus_sprintf_s_checked(int fmt_is_literal, size_t known_dmax, char *dest, size_t dmax, const char *fmt, ...) {
    cplus_safe_log(CPLUS_SAFE_LOG_TRACE, "sprintf_s", "enter");
    if (!fmt) { errno = EINVAL; cplus_safe_log(CPLUS_SAFE_LOG_ERROR, "sprintf_s", "null format"); cplus_safe_stat_record("sprintf_s", 0); return -1; }
    if (cplus_safe_ptr_was_freed(fmt)) return fmt_violation("sprintf_s", "use-after-free detected in format string");
    if (!fmt_is_literal) return fmt_violation("sprintf_s", "non-literal format string refused (CWE-134)");
    if (!dest && dmax != 0) { errno = EINVAL; cplus_safe_log(CPLUS_SAFE_LOG_ERROR, "sprintf_s", "invalid dest/dmax"); cplus_safe_stat_record("sprintf_s", 0); return -1; }
    if (fmt_check_dest_bounds("sprintf_s", dest, dmax, known_dmax) < 0) return -1;
    {
        va_list ap; va_start(ap, fmt);
        int n = cplus_vsprintf_s(dest, dmax, fmt, ap);
        va_end(ap);
        cplus_safe_stat_record("sprintf_s", n >= 0);
        return n;
    }
}
