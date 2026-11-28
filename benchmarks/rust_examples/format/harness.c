/*
 * C99 harness: calls the real checked format/scan/conversion APIs and prints
 * the same "<api> <case> <token>" lines as format.rs (safe-Rust examples).
 *
 * Link both archives (from the repo root):
 *   clang -std=c99 -O2 -D_DARWIN_C_SOURCE=1 harness.c \
 *       build/safe/libcplus_safecrt.a build/libc/libcplus_hardened.a -o harness
 *
 * Prototypes below are copied verbatim from:
 *   - stdlib/safe/safe_format.h            (fmt_is_literal / known_dmax forms)
 *   - libc/install-include/cplus_libc.h    (strtol / strtoul / strtoimax)
 * They are declared locally (without the GCC format attribute) so this harness
 * stays self-contained and so deliberately forbidden format strings such as
 * "AB%n" do not trigger host -Wformat diagnostics.
 *
 * POSIX dup()/dup2()/fileno() are used ONLY to keep printf_s's own output out
 * of the token stream; they do not affect the APIs under test.
 */

#define _DARWIN_C_SOURCE 1

#include <errno.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* --- stdlib/safe/safe_format.h ------------------------------------------- */
int cplus_sprintf_s(char *dest, size_t dmax, const char *fmt, ...);
int cplus_sprintf_s_checked(int fmt_is_literal, size_t known_dmax, char *dest,
                            size_t dmax, const char *fmt, ...);
int cplus_snprintf_s(char *dest, size_t dmax, const char *fmt, ...);
int cplus_snprintf_s_checked(int fmt_is_literal, size_t known_dmax, char *dest,
                             size_t dmax, const char *fmt, ...);
int cplus_vsnprintf_s(char *dest, size_t dmax, const char *fmt, va_list ap);
int cplus_vsprintf_s(char *dest, size_t dmax, const char *fmt, va_list ap);
int cplus_printf_s(const char *fmt, ...);
int cplus_printf_s_checked(int fmt_is_literal, const char *fmt, ...);
int cplus_fprintf_s(FILE *stream, const char *fmt, ...);
int cplus_fprintf_s_checked(int fmt_is_literal, FILE *stream, const char *fmt,
                            ...);
int cplus_sscanf_s(const char *str, const char *fmt, ...);
int cplus_sscanf_s_checked(int fmt_is_literal, const char *str,
                           const char *fmt, ...);

/* --- libc/install-include/cplus_libc.h ----------------------------------- */
long cplus_strtol(const char *nptr, char **endptr, int base);
unsigned long cplus_strtoul(const char *nptr, char **endptr, int base);
intmax_t cplus_strtoimax(const char *nptr, char **endptr, int base);

/* --- runtime / logging controls (stdlib/safe/safe_runtime.h, safe_log.h) - */
void cplus_runtime_set_violation_trap(int enabled);
void cplus_runtime_set_failstop(int enabled);
void cplus_safe_log_set_level(int level); /* CPLUS_SAFE_LOG_OFF == 0 */

#define VALUE 42
#define OVERFLOW_DECIMAL "99999999999999999999999999"

static void emit(const char *api, const char *case_name, const char *token) {
    printf("%s %s %s\n", api, case_name, token);
}

/* The bounded APIs return a C99-snprintf-style would-be length on success.
 * rc > dmax-1 therefore means TRUNC; any negative rc is a defined refusal. */
static const char *classify_bounded(int rc, size_t dmax) {
    if (rc < 0) return "REFUSED";
    return ((size_t)rc > dmax - 1u) ? "TRUNC" : "OK";
}

/* ------------------------------------------------------------------------- */
/* Buffer-format family: 6 APIs x 5 cases, same order as format.rs.          */
/* ------------------------------------------------------------------------- */

/* Every adapter is variadic so all six share one function-pointer type; each
 * reads the single int payload and forwards it to the corresponding API. */
typedef int (*buf_call)(char *dest, size_t cap, const char *fmt, ...);

static int a_sprintf_s(char *d, size_t c, const char *f, ...) {
    int v;
    va_list ap;
    va_start(ap, f); v = va_arg(ap, int); va_end(ap);
    return cplus_sprintf_s(d, c, f, v);
}
static int a_sprintf_s_checked(char *d, size_t c, const char *f, ...) {
    int v;
    va_list ap;
    va_start(ap, f); v = va_arg(ap, int); va_end(ap);
    return cplus_sprintf_s_checked(1, c, d, c, f, v);
}
static int a_snprintf_s(char *d, size_t c, const char *f, ...) {
    int v;
    va_list ap;
    va_start(ap, f); v = va_arg(ap, int); va_end(ap);
    return cplus_snprintf_s(d, c, f, v);
}
static int a_snprintf_s_checked(char *d, size_t c, const char *f, ...) {
    int v;
    va_list ap;
    va_start(ap, f); v = va_arg(ap, int); va_end(ap);
    return cplus_snprintf_s_checked(1, c, d, c, f, v);
}
static int forward_vsnprintf(char *d, size_t c, const char *f, va_list ap) {
    return cplus_vsnprintf_s(d, c, f, ap);
}
static int forward_vsprintf(char *d, size_t c, const char *f, va_list ap) {
    return cplus_vsprintf_s(d, c, f, ap);
}
/* va_start must live in a variadic function, so the normalized adapter is
 * variadic and forwards its va_list to a fixed-arity helper. */
static int a_vsnprintf_s(char *d, size_t c, const char *f, ...) {
    int n;
    va_list ap;
    va_start(ap, f);
    n = forward_vsnprintf(d, c, f, ap);
    va_end(ap);
    return n;
}
static int a_vsprintf_s(char *d, size_t c, const char *f, ...) {
    int n;
    va_list ap;
    va_start(ap, f);
    n = forward_vsprintf(d, c, f, ap);
    va_end(ap);
    return n;
}

static void run_buffer_api(const char *api, buf_call call) {
    char buf[16];

    /* valid: ample capacity, dmax = 16 */
    memset(buf, 0, sizeof buf);
    emit(api, "valid", classify_bounded(call(buf, sizeof buf, "AB%d", VALUE), sizeof buf));

    /* exact_fit: dmax = 5 holds "AB42\0" exactly (would-be length 4) */
    memset(buf, 0, sizeof buf);
    emit(api, "exact_fit", classify_bounded(call(buf, 5u, "AB%d", VALUE), 5u));

    /* one-byte-too-small: dmax = 4, needs 5 -> TRUNC with would-be length 4 */
    memset(buf, 0, sizeof buf);
    emit(api, "too_small", classify_bounded(call(buf, 4u, "AB%d", VALUE), 4u));

    /* null destination, dmax != 0 -> defined REFUSED (-1 / EINVAL) */
    emit(api, "null_dest", classify_bounded(call(NULL, 5u, "AB%d", VALUE), 5u));

    /* forbidden %n -> fmt_violation -> defined REFUSED (-1 / EINVAL).
     * The API refuses before executing the conversion, so no argument is
     * consumed; passing VALUE is harmless. */
    emit(api, "percent_n", classify_bounded(call(buf, sizeof buf, "AB%n", VALUE), sizeof buf));
}

/* Redirect printf_s's own stdout into a scratch file for the duration of one
 * call, so the emitted token lines stay parseable. */
#define CAPTURE_STDOUT_RC(rc, expr)                                            \
    do {                                                                       \
        fflush(stdout);                                                        \
        {                                                                      \
            int saved_fd_ = dup(STDOUT_FILENO);                                \
            FILE *sink_ = tmpfile();                                           \
            if (saved_fd_ >= 0 && sink_ != NULL) {                             \
                dup2(fileno(sink_), STDOUT_FILENO);                            \
            }                                                                  \
            (rc) = (expr);                                                     \
            fflush(stdout);                                                    \
            if (saved_fd_ >= 0 && sink_ != NULL) {                             \
                dup2(saved_fd_, STDOUT_FILENO);                                \
                close(saved_fd_);                                              \
                fclose(sink_);                                                 \
            }                                                                  \
        }                                                                      \
    } while (0)

/* ------------------------------------------------------------------------- */
/* Stream-format family.                                                     */
/* ------------------------------------------------------------------------- */

static void run_printf_api(const char *api, int checked) {
    int rc;
    if (checked) {
        CAPTURE_STDOUT_RC(rc, cplus_printf_s_checked(1, "AB%d", VALUE));
    } else {
        CAPTURE_STDOUT_RC(rc, cplus_printf_s("AB%d", VALUE));
    }
    emit(api, "valid", rc >= 0 ? "OK" : "REFUSED");

    if (checked) {
        rc = cplus_printf_s_checked(1, "AB%n", VALUE);
    } else {
        rc = cplus_printf_s("AB%n", VALUE);
    }
    emit(api, "percent_n", rc < 0 ? "REFUSED" : "OK");
}

static void run_fprintf_api(const char *api, int checked) {
    FILE *sink;
    int rc;

    sink = tmpfile();
    rc = checked ? cplus_fprintf_s_checked(1, sink, "AB%d", VALUE)
                 : cplus_fprintf_s(sink, "AB%d", VALUE);
    fclose(sink);
    emit(api, "valid", rc >= 0 ? "OK" : "REFUSED");

    sink = tmpfile();
    rc = checked ? cplus_fprintf_s_checked(1, sink, "AB%n", VALUE)
                 : cplus_fprintf_s(sink, "AB%n", VALUE);
    fclose(sink);
    emit(api, "percent_n", rc < 0 ? "REFUSED" : "OK");

    rc = checked ? cplus_fprintf_s_checked(1, NULL, "AB%d", VALUE)
                 : cplus_fprintf_s(NULL, "AB%d", VALUE);
    emit(api, "null_stream", rc < 0 ? "REFUSED" : "OK");
}

/* ------------------------------------------------------------------------- */
/* Scan family: 2 APIs x 4 cases.                                            */
/* ------------------------------------------------------------------------- */

static void run_scan_api(const char *api, int checked) {
    int value = 0;
    int rc;
    int dummy = 0;
    char scratch[16];

    rc = checked ? cplus_sscanf_s_checked(1, "42", "%d", &value)
                 : cplus_sscanf_s("42", "%d", &value);
    emit(api, "valid", (rc == 1 && value == VALUE) ? "OK" : "REFUSED");

    rc = checked ? cplus_sscanf_s_checked(1, "42", "%n", &dummy)
                 : cplus_sscanf_s("42", "%n", &dummy);
    emit(api, "percent_n", rc < 0 ? "REFUSED" : "OK");

    /* Unbounded %s has no field width -> scanf width enforcement refuses it. */
    rc = checked ? cplus_sscanf_s_checked(1, "abc", "%s", scratch)
                 : cplus_sscanf_s("abc", "%s", scratch);
    emit(api, "unbounded_s", rc < 0 ? "REFUSED" : "OK");

    rc = checked ? cplus_sscanf_s_checked(1, NULL, "%d", &value)
                 : cplus_sscanf_s(NULL, "%d", &value);
    emit(api, "null_input", rc < 0 ? "REFUSED" : "OK");
}

/* ------------------------------------------------------------------------- */
/* Conversion family. Numeric overflow is a defined refusal: the wrappers
 * clamp to LONG_MAX/ULONG_MAX/INTMAX_MAX and set errno == ERANGE.            */
/* ------------------------------------------------------------------------- */

static void run_strtol(void) {
    const char *s;
    char *end;
    long v;

    s = "42";
    end = NULL;
    errno = 0;
    v = cplus_strtol(s, &end, 10);
    emit("strtol", "valid", (errno == 0 && end != s && v == VALUE) ? "OK" : "REFUSED");

    s = OVERFLOW_DECIMAL;
    end = NULL;
    errno = 0;
    v = cplus_strtol(s, &end, 10);
    (void)v;
    emit("strtol", "overflow", (errno == ERANGE) ? "REFUSED" : "OK");

    s = "abc";
    end = NULL;
    errno = 0;
    v = cplus_strtol(s, &end, 10);
    (void)v;
    emit("strtol", "invalid", (end == s) ? "REFUSED" : "OK");
}

static void run_strtoul(void) {
    const char *s;
    char *end;
    unsigned long v;

    s = "42";
    end = NULL;
    errno = 0;
    v = cplus_strtoul(s, &end, 10);
    emit("strtoul", "valid", (errno == 0 && end != s && v == 42ul) ? "OK" : "REFUSED");

    s = OVERFLOW_DECIMAL;
    end = NULL;
    errno = 0;
    v = cplus_strtoul(s, &end, 10);
    (void)v;
    emit("strtoul", "overflow", (errno == ERANGE) ? "REFUSED" : "OK");

    s = "abc";
    end = NULL;
    errno = 0;
    v = cplus_strtoul(s, &end, 10);
    (void)v;
    emit("strtoul", "invalid", (end == s) ? "REFUSED" : "OK");
}

static void run_strtoimax(void) {
    const char *s;
    char *end;
    intmax_t v;

    s = "42";
    end = NULL;
    errno = 0;
    v = cplus_strtoimax(s, &end, 10);
    emit("strtoimax", "valid", (errno == 0 && end != s && v == VALUE) ? "OK" : "REFUSED");

    s = OVERFLOW_DECIMAL;
    end = NULL;
    errno = 0;
    v = cplus_strtoimax(s, &end, 10);
    (void)v;
    emit("strtoimax", "overflow", (errno == ERANGE) ? "REFUSED" : "OK");

    s = "abc";
    end = NULL;
    errno = 0;
    v = cplus_strtoimax(s, &end, 10);
    (void)v;
    emit("strtoimax", "invalid", (end == s) ? "REFUSED" : "OK");

    /* Unlike cplus_strtol/cplus_strtoul (thin libc passthroughs), this wrapper
     * guards nptr == NULL explicitly: errno = EINVAL, *endptr = NULL, rc = 0. */
    end = NULL;
    errno = 0;
    v = cplus_strtoimax(NULL, &end, 10);
    emit("strtoimax", "null_input", (v == 0 && errno == EINVAL && end == NULL) ? "REFUSED" : "OK");
}

int main(void) {
    /* Keep violations recoverable and logging quiet so tokens are the only
     * thing on stdout and the harness never _exit(70) on a refusal case. */
    cplus_safe_log_set_level(0);
    cplus_runtime_set_failstop(0);
    cplus_runtime_set_violation_trap(0);

    run_buffer_api("sprintf_s", a_sprintf_s);
    run_buffer_api("sprintf_s_checked", a_sprintf_s_checked);
    run_buffer_api("snprintf_s", a_snprintf_s);
    run_buffer_api("snprintf_s_checked", a_snprintf_s_checked);
    run_buffer_api("vsnprintf_s", a_vsnprintf_s);
    run_buffer_api("vsprintf_s", a_vsprintf_s);

    run_printf_api("printf_s", 0);
    run_printf_api("printf_s_checked", 1);
    run_fprintf_api("fprintf_s", 0);
    run_fprintf_api("fprintf_s_checked", 1);

    run_scan_api("sscanf_s", 0);
    run_scan_api("sscanf_s_checked", 1);

    run_strtol();
    run_strtoul();
    run_strtoimax();

    return 0;
}
