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
#include <stdlib.h>
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

/* One conversion specifier, with the offsets needed to rewrite it safely.
 * `start` points at '%', `width_at` at the first width character (right after
 * flags/positional), `conv_at` at the conversion character and `end` just past
 * the whole specifier (including a scanset). */
typedef struct {
    const unsigned char *start;
    const unsigned char *width_at;
    const unsigned char *conv_at;
    const unsigned char *end;
    int conv;        /* 0 = dangling '%' at end of string */
    int has_width;
    int suppressed;
    int bounded;     /* s, [, POSIX S, POSIX C: Annex K takes a capacity arg */
} fmt_spec_t;

/* Parse one specifier.  Returns 1 when a specifier was consumed, 0 for an
 * ordinary character (p advanced by one), -1 for a forbidden positional
 * marker.  Flags and length modifiers include glibc `I` / `'` / `m`, MS `I64`
 * and ISO C23 `wN` / `wfN`: stopping early on any of them used to hide a
 * following 'n' from the filter (issue #192). */
static int fmt_parse_spec(const unsigned char **pp, fmt_spec_t *s) {
    const unsigned char *p = *pp;
    int nonzero_width = 0;
    memset(s, 0, sizeof(*s));
    if (*p != '%') { *pp = p + 1; return 0; }
    s->start = p;
    ++p;
    if (*p == '%') {
        s->conv_at = p;
        s->conv = '%';
        s->end = p + 1;
        *pp = s->end;
        return 1;
    }
    if (parse_pos_dollar(&p) < 0) return -1;
    if (*p == '*') { s->suppressed = 1; ++p; if (parse_pos_dollar(&p) < 0) return -1; }
    for (;;) {
        unsigned char c = *p;
        if (c == '-' || c == '+' || c == ' ' || c == '#' || c == '0' || c == '\'') { ++p; continue; }
        /* `I`/`m` are flags only when something follows them; a bare `%m`
         * (glibc strerror) is itself a conversion and must not be skipped. */
        if ((c == 'I' || c == 'm') && p[1] != '\0' && p[1] != '$' && p[1] != '%') { ++p; continue; }
        break;
    }
    s->width_at = p;
    if (*p == '*') {
        ++p;
        s->has_width = 1;
        if (parse_pos_dollar(&p) < 0) return -1;
    } else {
        const unsigned char *w = p;
        while (*p >= '0' && *p <= '9') {
            if (*p != '0') nonzero_width = 1;
            ++p;
        }
        if (p > w && nonzero_width) s->has_width = 1; /* %0s is NOT bounded */
    }
    if (*p == '.') {
        ++p;
        if (*p == '*') {
            ++p;
            if (parse_pos_dollar(&p) < 0) return -1;
        } else {
            while (*p >= '0' && *p <= '9') ++p;
        }
    }
    if (*p == '$') return -1; /* repeated or misplaced positional marker */
    for (;;) {
        unsigned char c = *p;
        if (c == 'h' || c == 'l' || c == 'j' || c == 'z' || c == 't' || c == 'L' || c == 'q') { ++p; continue; }
        if (c == 'I') { ++p; while (*p >= '0' && *p <= '9') ++p; continue; }
        if (c == 'w') { /* C23 exact-width: wN, wfN */
            ++p;
            if (*p == 'f') ++p;
            while (*p >= '0' && *p <= '9') ++p;
            continue;
        }
        break;
    }
    if (*p == '$') return -1;
    s->conv_at = p;
    s->conv = *p;
    s->end = p;
    if (*p == '\0') { *pp = p; return 1; } /* dangling '%': no conversion */
    ++p;
    if (s->conv == '[') {
        if (*p == '^') ++p;
        if (*p == ']') ++p;
        while (*p && *p != ']') ++p;
        if (*p == ']') ++p;
    }
    s->end = p;
    s->bounded = (s->conv == 's' || s->conv == '[' || s->conv == 'S' || s->conv == 'C');
    *pp = p;
    return 1;
}

static int cplus_fmt_has_percent_n(const char *fmt) {
    const unsigned char *p = (const unsigned char *)fmt;
    fmt_spec_t s;
    int r;
    if (!p) return 0;
    /* C+ SECURITY PATCH: every '$' positional marker is refused.  libc accepts
     * '$' in many positions, and any missed placement can turn a later 'n'
     * into a real write.  Literal '$' in ordinary text is unaffected. */
    while (*p) {
        r = fmt_parse_spec(&p, &s);
        if (r < 0) return 1;
        if (r > 0 && s.conv == 'n') return 1;
    }
    return 0;
}

/* Returns 0 if safe, 1 if %n / invalid positional present, 2 if an unbounded
 * string conversion is present. */
static int cplus_scanf_fmt_check(const char *fmt) {
    const unsigned char *p = (const unsigned char *)fmt;
    fmt_spec_t s;
    int r;
    if (!p) return 0;
    while (*p) {
        r = fmt_parse_spec(&p, &s);
        if (r < 0) return 1;
        if (r == 0 || s.conv == 0 || s.conv == '%') continue;
        if (s.conv == 'n') return 1;
        if (s.bounded && s.conv != 'C' && !s.suppressed && !s.has_width) return 2;
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
        /* (size_t)-1 is the "not in a tracked live block" sentinel, not a
         * capacity: treating it as one made every dmax check pass for stack
         * and static destinations.  Use the remaining bytes of a tracked
         * block (which also covers interior heap pointers, including a
         * proven zero-byte one-past-the-end destination), and otherwise fall
         * back to the tracked base size / caller's known dmax.  Keep -1 for
         * a genuinely unknown capacity so the existing permissive policy is
         * preserved there. */
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
    /* Issue 2 (unbounded %s / %[) is enforced by scan_dispatch() after the
     * argument shape is known: an Annex K capacity makes an unbounded "%s"
     * safe, while the POSIX-style call must still be refused. */
    if (issue == 1) return fmt_violation(api, "forbidden %n specifier in scanf");
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
/* ---- Annex K / POSIX scanf argument-shape dispatcher -------------------
 *
 * The exported scanf_s family historically forwarded the caller's va_list to
 * host vscanf/vfscanf/vsscanf.  Annex K callers pass an extra rsize_t capacity
 * after every %s/%c/%[ pointer; host scanf does not consume it, so the next
 * conversion reads the capacity value as a POINTER and writes to address
 * 0x10 (SIGSEGV).  We now inspect the argument shape once:
 *   - POSIX shape (no capacities): call the host exactly as before;
 *   - Annex K shape: collect the pointer arguments, rewrite unbounded/oversize
 *     string conversions with a width derived from the caller's capacity, and
 *     forward only the pointer arguments.  A capacity is recognised as a small
 *     non-zero integer immediately following a bounded conversion's pointer;
 *     real user pointers on the supported ABIs are far larger.
 * All arguments to scanf conversions are pointer-sized on the supported ABIs,
 * including the capacity values, so the shape inspection uses void* words. */
#define CPLUS_SCAN_MAX_ARGS 16u
#if UINTPTR_MAX > 0xffffffffu
#define CPLUS_SCAN_CAP_MAX ((uintptr_t)0x1000000u)
#else
#define CPLUS_SCAN_CAP_MAX ((uintptr_t)0x10000u)
#endif

#define SCAN_FWD_DEFINE(N)                                                  \
static int scan_fwd_##N(int mode, FILE *stream, const char *str,             \
                        const char *fmt, ...) {                              \
    va_list ap;                                                              \
    int r;                                                                   \
    va_start(ap, fmt);                                                       \
    if (mode == 1) r = vfscanf(stream, fmt, ap);                             \
    else if (mode == 2) r = vsscanf(str, fmt, ap);                           \
    else r = vscanf(fmt, ap);                                                \
    va_end(ap);                                                              \
    return r;                                                                \
}

static int scan_fwd_0(int mode, FILE *stream, const char *str, const char *fmt, ...) {
    va_list ap;
    int r;
    va_start(ap, fmt);
    if (mode == 1) r = vfscanf(stream, fmt, ap);
    else if (mode == 2) r = vsscanf(str, fmt, ap);
    else r = vscanf(fmt, ap);
    va_end(ap);
    return r;
}
SCAN_FWD_DEFINE(1)
SCAN_FWD_DEFINE(2)
SCAN_FWD_DEFINE(3)
SCAN_FWD_DEFINE(4)
SCAN_FWD_DEFINE(5)
SCAN_FWD_DEFINE(6)
SCAN_FWD_DEFINE(7)
SCAN_FWD_DEFINE(8)
SCAN_FWD_DEFINE(9)
SCAN_FWD_DEFINE(10)
SCAN_FWD_DEFINE(11)
SCAN_FWD_DEFINE(12)
SCAN_FWD_DEFINE(13)
SCAN_FWD_DEFINE(14)
SCAN_FWD_DEFINE(15)
SCAN_FWD_DEFINE(16)

static int scan_fwd(int mode, FILE *stream, const char *str, const char *fmt,
                    void *const *args, size_t nargs) {
    switch (nargs) {
    case 0:  return scan_fwd_0(mode, stream, str, fmt);
    case 1:  return scan_fwd_1(mode, stream, str, fmt, args[0]);
    case 2:  return scan_fwd_2(mode, stream, str, fmt, args[0], args[1]);
    case 3:  return scan_fwd_3(mode, stream, str, fmt, args[0], args[1], args[2]);
    case 4:  return scan_fwd_4(mode, stream, str, fmt, args[0], args[1], args[2], args[3]);
    case 5:  return scan_fwd_5(mode, stream, str, fmt, args[0], args[1], args[2], args[3], args[4]);
    case 6:  return scan_fwd_6(mode, stream, str, fmt, args[0], args[1], args[2], args[3], args[4], args[5]);
    case 7:  return scan_fwd_7(mode, stream, str, fmt, args[0], args[1], args[2], args[3], args[4], args[5], args[6]);
    case 8:  return scan_fwd_8(mode, stream, str, fmt, args[0], args[1], args[2], args[3], args[4], args[5], args[6], args[7]);
    case 9:  return scan_fwd_9(mode, stream, str, fmt, args[0], args[1], args[2], args[3], args[4], args[5], args[6], args[7], args[8]);
    case 10: return scan_fwd_10(mode, stream, str, fmt, args[0], args[1], args[2], args[3], args[4], args[5], args[6], args[7], args[8], args[9]);
    case 11: return scan_fwd_11(mode, stream, str, fmt, args[0], args[1], args[2], args[3], args[4], args[5], args[6], args[7], args[8], args[9], args[10]);
    case 12: return scan_fwd_12(mode, stream, str, fmt, args[0], args[1], args[2], args[3], args[4], args[5], args[6], args[7], args[8], args[9], args[10], args[11]);
    case 13: return scan_fwd_13(mode, stream, str, fmt, args[0], args[1], args[2], args[3], args[4], args[5], args[6], args[7], args[8], args[9], args[10], args[11], args[12]);
    case 14: return scan_fwd_14(mode, stream, str, fmt, args[0], args[1], args[2], args[3], args[4], args[5], args[6], args[7], args[8], args[9], args[10], args[11], args[12], args[13]);
    case 15: return scan_fwd_15(mode, stream, str, fmt, args[0], args[1], args[2], args[3], args[4], args[5], args[6], args[7], args[8], args[9], args[10], args[11], args[12], args[13], args[14]);
    default: return scan_fwd_16(mode, stream, str, fmt, args[0], args[1], args[2], args[3], args[4], args[5], args[6], args[7], args[8], args[9], args[10], args[11], args[12], args[13], args[14], args[15]);
    }
}

/* mode: 0 = vscanf, 1 = vfscanf, 2 = vsscanf. */
static int scan_dispatch(const char *api, int mode, FILE *stream, const char *str,
                         const char *fmt, va_list ap) {
    fmt_spec_t specs[CPLUS_SCAN_MAX_ARGS];
    void *args[CPLUS_SCAN_MAX_ARGS];
    size_t caps[CPLUS_SCAN_MAX_ARGS];
    size_t nspecs = 0, nargs = 0, flen, pos = 0, i, n;
    const unsigned char *p = (const unsigned char *)fmt;
    const unsigned char *last = p;
    int r, rc, annexk = 0, have_pending = 0, unbounded_posix = 0;
    void *pending = NULL;
    va_list peek;
    char *newfmt;

    while (*p) {
        r = fmt_parse_spec(&p, &specs[nspecs]);
        if (r < 0) return fmt_violation(api, "malformed scanf format");
        if (r == 0) continue;
        if (specs[nspecs].conv != 0) {
            if (specs[nspecs].bounded && !specs[nspecs].suppressed &&
                !specs[nspecs].has_width && specs[nspecs].conv != 'C')
                unbounded_posix = 1;
            if (++nspecs == CPLUS_SCAN_MAX_ARGS) {
                while (*p) { r = fmt_parse_spec(&p, &specs[0]); if (r < 0) return fmt_violation(api, "malformed scanf format"); }
                return fmt_violation(api, "too many scanf conversions");
            }
        }
    }

    memset(args, 0, sizeof(args));
    memset(caps, 0, sizeof(caps));
    va_copy(peek, ap);
    for (i = 0; i < nspecs; ++i) {
        const fmt_spec_t *s = &specs[i];
        void *word;
        if (s->conv == '%' || s->suppressed) continue;
        word = have_pending ? pending : va_arg(peek, void *);
        have_pending = 0;
        if (nargs == CPLUS_SCAN_MAX_ARGS) { va_end(peek); return fmt_violation(api, "too many scanf arguments"); }
        args[nargs++] = word;
        if (!s->bounded) continue;
        {
            /* Never read past the caller's va_list on a lone bounded
             * conversion with no width: that is the POSIX shape (and the
             * historically refused unbounded "%s"), and the extra va_arg
             * would inspect an uninitialised register save slot. */
            size_t j;
            int peek_ok = 0;
            for (j = i + 1; j < nspecs && !peek_ok; ++j)
                if (specs[j].conv != 0 && specs[j].conv != '%' && !specs[j].suppressed)
                    peek_ok = 1;
            if (!peek_ok) continue;
            {
            void *next = va_arg(peek, void *);
            uintptr_t v = (uintptr_t)next;
            if (v != 0 && v <= CPLUS_SCAN_CAP_MAX) {
                caps[i] = (size_t)v;
                annexk = 1;
            } else {
                pending = next;
                have_pending = 1;
            }
            }
        }
    }
    va_end(peek);

    if (!annexk) {
        if (unbounded_posix)
            return fmt_violation(api, "unbounded %s or %[ specifier in scanf without field width");
        if (mode == 1) rc = vfscanf(stream, fmt, ap);
        else if (mode == 2) rc = vsscanf(str, fmt, ap);
        else rc = vscanf(fmt, ap);
        return rc;
    }

    flen = strlen(fmt);
    if (flen > (SIZE_MAX / 2u) - 64u) return fmt_violation(api, "scanf format too long");
    newfmt = (char *)malloc(flen * 2u + 64u);
    if (!newfmt) { errno = ENOMEM; return -1; }
    for (i = 0; i < nspecs; ++i) {
        const fmt_spec_t *s = &specs[i];
        size_t n = (size_t)(s->start - last);
        memcpy(newfmt + pos, last, n);
        pos += n;
        if (s->conv == '%' || s->suppressed || !s->bounded || caps[i] == 0) {
            n = (size_t)(s->end - s->start);
            memcpy(newfmt + pos, s->start, n);
            pos += n;
        } else {
            size_t cap = caps[i];
            size_t limit;
            const unsigned char *we = s->width_at;
            char num[32];
            int k;
            if (s->conv == 'c' || s->conv == 'C') {
                if (cap == 0) { free(newfmt); return fmt_violation(api, "zero scanf capacity"); }
                n = (size_t)(s->end - s->start);
                memcpy(newfmt + pos, s->start, n);
                pos += n;
                last = s->end;
                continue;
            }
            if (cap < 2u) { free(newfmt); return fmt_violation(api, "scanf capacity too small"); }
            limit = cap - 1u;
            n = (size_t)(s->width_at - s->start);
            memcpy(newfmt + pos, s->start, n);
            pos += n;
            while (*we >= '0' && *we <= '9') ++we;
            if (we > s->width_at) {
                unsigned long long w = 0;
                const unsigned char *d = s->width_at;
                while (d < we) {
                    if (w < 1000000000ULL) w = w * 10ULL + (unsigned long long)(*d - '0');
                    ++d;
                }
                if (w > 0 && w < (unsigned long long)limit) limit = (size_t)w;
            }
            k = snprintf(num, sizeof(num), "%zu", limit);
            if (k < 0 || (size_t)k >= sizeof(num)) { free(newfmt); return fmt_violation(api, "scanf width overflow"); }
            memcpy(newfmt + pos, num, (size_t)k);
            pos += (size_t)k;
            n = (size_t)(s->end - we);
            memcpy(newfmt + pos, we, n);
            pos += n;
        }
        last = s->end;
    }
    n = strlen((const char *)last);
    memcpy(newfmt + pos, last, n);
    pos += n;
    newfmt[pos] = '\0';
    rc = scan_fwd(mode, stream, str, newfmt, args, nargs);
    free(newfmt);
    return rc;
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
