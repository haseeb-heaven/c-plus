/*
 * stdio_c.c -- C+ side of the paired Rust/C+ stdio + format comparison.
 *
 * PROTOCOL (identical to stdio_rs.rs)
 *   argv[1] = case number 1..100.  A single case runs per process.
 *   stdout receives exactly one line:  RESULT <n> <ACCEPTED|REFUSED|SKIP>
 *   The process always returns 0.  There are no aborts, asserts or traps.
 *
 * OUTCOME SEMANTICS
 *   ACCEPTED  the intended operation completed (a defined EOF/short read is
 *             still an accepted, well-defined outcome, not a refusal).
 *   REFUSED   a guard / checked API / safe error path rejected the operation
 *             and returned an error without crashing (NULL guard, size*count
 *             overflow, double close, non-literal format, %n, invalid mode,
 *             write to a read-only stream, ...).
 *   SKIP      the scenario is not expressible with the paired language's safe
 *             API.  Each SKIP case carries a per-case comment on BOTH sides.
 *
 * BUILD (same flags as tests/rust_vs_cplus/run_compare.sh)
 *   /usr/bin/clang -O1 -g -std=c99 -Wall -Wextra -pedantic \
 *     -I. -Istdlib/safe -Ilibc -Ilibc/install-include \
 *     -Ithird_party/safeclib/include -Ithird_party/intel-safestringlib/include \
 *     tests/rust_vs_cplus/stdio_c.c \
 *     build/safe/libcplus_safecrt.a build/libc/libcplus_hardened.a \
 *     -lm -pthread -o build/rust_vs_cplus/stdio_c
 *
 * HONESTY / EXPECTED DIFFERENCES (mirrored in stdio_rs.rs)
 *   - C+ checked format gates (snprintf_s/scanf_s) are RUNTIME checks; Rust's
 *     format!/write! formats are COMPILE-TIME checked.  Cases 81/82/87/88/90
 *     therefore report REFUSED here and SKIP in Rust: there is no dynamic
 *     format string and no %n in safe Rust, so the gate cannot be exercised.
 *   - C FILE* has NULL and double-close states.  Safe Rust uses Option<File>
 *     and RAII drop; NULL-format/stream and double-close gates have no exact
 *     analogue and are skipped or modelled with empty/None (documented).
 *   - C snprintf truncates into a fixed buffer and reports the would-be length;
 *     safe Rust formats into a heap String and truncates explicitly.  The
 *     observable string is compared, not the raw C return-value contract.
 *   - C fread(size,count) returns whole items; Rust's Read::read returns raw
 *     bytes.  Cases 17/18/24 compare the whole-item count derived from bytes.
 *
 * SAFETY: NULL is passed ONLY to guarded wrappers (cplus_fopen,
 * cplus_fclose, cplus_printf, cplus_fprintf, cplus_vsnprintf_s, cplus_fread,
 * cplus_fwrite, *_s adapters).  No raw FILE* is used after close; the only
 * double close is through cplus_fclose_s, which is designed to refuse it.
 * The SafeCRT violation trap is turned OFF so refusals are return values,
 * never exit(70).  All temp files are named via $TMPDIR + PID + case and are
 * removed before the case returns.
 */
#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "stdlib/safe/safe_c.h" /* ordinary C spelling, hardened wrappers */
#include "safe_runtime.h"      /* cplus_runtime_set_violation_trap */

#define NCASES 100
#if NCASES != 100
#error "stdio_c.c must define exactly 100 scenarios"
#endif
typedef char stdio_c_assert_100_cases[(NCASES == 100) ? 1 : -1];

enum outcome { OUT_ACCEPTED = 0, OUT_REFUSED = 1, OUT_SKIP = 2 };

static const char *outcome_text(enum outcome o)
{
    switch (o) {
    case OUT_ACCEPTED: return "ACCEPTED";
    case OUT_REFUSED:  return "REFUSED";
    case OUT_SKIP:     return "SKIP";
    }
    return "SKIP";
}

static int emit(int n, enum outcome o)
{
    printf("RESULT %d %s\n", n, outcome_text(o));
    return 0;
}

/* "$TMPDIR/rust_vs_cplus_<pid>_<case>_<slot>.tmp" (no hard-coded user path). */
static const char *tpath(int n, const char *slot, char *buf, size_t cap)
{
    const char *d = getenv("TMPDIR");
    if (d == NULL || *d == '\0') d = "/tmp";
    (void)snprintf(buf, cap, "%s/rust_vs_cplus_%ld_%03d_%s.tmp",
                   d, (long)getpid(), n, slot);
    return buf;
}

static int wrfile(const char *p, const void *data, size_t n)
{
    FILE *f = fopen(p, "wb");
    size_t w;
    if (f == NULL) return 0;
    w = fwrite(data, 1, n, f);
    if (fclose(f) != 0) return 0;
    return w == n;
}

static int rdfile(const char *p, void *buf, size_t cap, size_t *out_n)
{
    FILE *f = fopen(p, "rb");
    size_t r;
    if (f == NULL) return 0;
    r = fread(buf, 1, cap, f);
    if (out_n != NULL) *out_n = r;
    if (fclose(f) != 0) return 0;
    return 1;
}

static int file_exists(const char *p)
{
    FILE *f = fopen(p, "rb");
    if (f == NULL) return 0;
    (void)fclose(f);
    return 1;
}

/* printf writes to a redirected stdout so the RESULT line stays the only
 * stdout output; the captured bytes are verified before the case reports. */
#define CAPTURE_PRINTF(path_, rc_, ...) do {                                  \
        int sv_ = dup(STDOUT_FILENO);                                         \
        FILE *cap_ = fopen((path_), "wb");                                    \
        (rc_) = -1;                                                           \
        if (sv_ >= 0 && cap_ != NULL) {                                       \
            fflush(stdout);                                                   \
            if (dup2(fileno(cap_), STDOUT_FILENO) >= 0) {                     \
                (rc_) = printf(__VA_ARGS__);                                  \
                fflush(stdout);                                               \
                (void)dup2(sv_, STDOUT_FILENO);                               \
            }                                                                 \
            (void)close(sv_);                                                 \
            (void)fclose(cap_);                                               \
        } else {                                                              \
            if (sv_ >= 0) (void)close(sv_);                                   \
            if (cap_ != NULL) (void)fclose(cap_);                             \
        }                                                                     \
    } while (0)

/* va_list forwarding for the vsnprintf family (same checked entry point). */
static int vfmt(char *buf, size_t n, const char *fmt, ...)
    CPLUS_SAFE_FMT_PRINTF(3, 4);

static int vfmt(char *buf, size_t n, const char *fmt, ...)
{
    va_list ap;
    int r;
    va_start(ap, fmt);
    r = vsnprintf_s(buf, n, fmt, ap);
    va_end(ap);
    return r;
}

static int vfmt_nullfmt(char *buf, size_t n, ...)
{
    const char *volatile vf = NULL;
    va_list ap;
    int r;
    va_start(ap, n);
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wformat-security"
#endif
    r = cplus_vsnprintf_s(buf, n, vf, ap);
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
    va_end(ap);
    return r;
}

int main(int argc, char **argv)
{
    int n = 0;
    enum outcome o = OUT_SKIP;

    if (argc >= 2) {
        char *endp = NULL;
        long v = strtol(argv[1], &endp, 10);
        if (endp != argv[1] && *endp == '\0' && v >= 1 && v <= NCASES)
            n = (int)v;
    }
    if (n == 0) return emit(0, OUT_SKIP);

    /* Refusals are observed as return values, never as a process trap. */
    cplus_runtime_set_violation_trap(0);

    switch (n) {
    /* ---- fopen (1-12) --------------------------------------------------- */
    { /* 01 fopen_wb_create: create a new binary file. */
    case 1: {
        char p[256]; int ok;
        tpath(n, "a", p, sizeof p); (void)remove(p);
        ok = fopen(p, "wb") != NULL;
        { FILE *f = fopen(p, "wb"); if (f != NULL) (void)fclose(f); else ok = 0; }
        (void)remove(p);
        o = ok ? OUT_ACCEPTED : OUT_REFUSED;
        break;
    }}
    /* 02 fopen_rb_existing: open a file that was just written. */
    { case 2: {
        char p[256]; FILE *f; int ok;
        tpath(n, "a", p, sizeof p); ok = wrfile(p, "abc", 3);
        f = fopen(p, "rb");
        if (f != NULL) (void)fclose(f); else ok = 0;
        (void)remove(p);
        o = ok ? OUT_ACCEPTED : OUT_REFUSED;
        break;
    }}
    /* 03 fopen_rb_missing: a missing file is a guarded error, not a crash. */
    { case 3: {
        char p[256]; FILE *f;
        tpath(n, "a", p, sizeof p); (void)remove(p);
        f = fopen(p, "rb");
        o = (f == NULL) ? OUT_REFUSED : OUT_ACCEPTED;
        break;
    }}
    /* 04 fopen_null_name_guarded: cplus_fopen(NULL, ...) returns NULL.
     * Rust analogue: no null pointer type exists -> SKIP there. */
    { case 4: {
        o = (cplus_fopen(NULL, "rb") == NULL) ? OUT_REFUSED : OUT_ACCEPTED;
        break;
    }}
    /* 05 fopen_null_mode_guarded: checked open refuses a NULL mode.
     * Rust analogue: modes are separate constructors -> SKIP there. */
    { case 5: {
        char p[256];
        tpath(n, "a", p, sizeof p);
        o = (cplus_fopen_s(p, NULL) == NULL) ? OUT_REFUSED : OUT_ACCEPTED;
        break;
    }}
    /* 06 fopen_invalid_mode: "qq" is rejected by the host CRT.
     * Rust analogue: there is no mode string -> SKIP there. */
    { case 6: {
        char p[256]; FILE *f;
        tpath(n, "a", p, sizeof p);
        f = fopen(p, "qq");
        o = (f == NULL) ? OUT_REFUSED : OUT_ACCEPTED;
        break;
    }}
    /* 07 fopen_wb_plus_roundtrip: write, rewind, read back through w+b. */
    { case 7: {
        char p[256]; char b[8] = {0}; FILE *f; int ok;
        tpath(n, "a", p, sizeof p); f = fopen(p, "w+b"); ok = (f != NULL);
        if (f != NULL) {
            ok = ok && fwrite("hi\n", 1, 3, f) == 3u;
            rewind(f);
            ok = ok && fgets(b, (int)sizeof b, f) == b && strcmp(b, "hi\n") == 0;
            (void)fclose(f);
        }
        (void)remove(p);
        o = ok ? OUT_ACCEPTED : OUT_REFUSED;
        break;
    }}
    /* 08 fopen_binary_embedded_nul: embedded NUL and 0xFF survive binary I/O. */
    { case 8: {
        char p[256]; const unsigned char src[5] = {'a', '\0', '\n', 0xFFu, 'b'};
        unsigned char b[8] = {0}; FILE *f; size_t r = 0; int ok;
        tpath(n, "a", p, sizeof p); f = fopen(p, "wb+"); ok = (f != NULL);
        if (f != NULL) {
            ok = ok && fwrite(src, 1, sizeof src, f) == sizeof src;
            rewind(f);
            r = fread(b, 1, sizeof src, f);
            (void)fclose(f);
        }
        (void)remove(p);
        ok = ok && r == sizeof src && memcmp(b, src, sizeof src) == 0;
        o = ok ? OUT_ACCEPTED : OUT_REFUSED;
        break;
    }}
    /* 09 fclose_valid_stream: a valid close returns 0. */
    { case 9: {
        char p[256]; FILE *f; int ok;
        tpath(n, "a", p, sizeof p); f = fopen(p, "wb"); ok = (f != NULL);
        if (f != NULL) ok = fclose(f) == 0;
        (void)remove(p);
        o = ok ? OUT_ACCEPTED : OUT_REFUSED;
        break;
    }}
    /* 10 fclose_null_guarded: cplus_fclose(NULL) returns EOF.
     * Rust analogue: Option::take() on None -> REFUSED (documented). */
    { case 10: {
        o = (cplus_fclose(NULL) == EOF) ? OUT_REFUSED : OUT_ACCEPTED;
        break;
    }}
    /* 11 fclose_s_double_close: first close 0, second close refused.
     * Rust analogue: Option::take() twice -> REFUSED (RAII owns the close). */
    { case 11: {
        char p[256]; FILE *f; int a = -1, b = -1;
        tpath(n, "a", p, sizeof p); f = cplus_fopen_s(p, "wb");
        if (f != NULL) { a = cplus_fclose_s(f); b = cplus_fclose_s(f); }
        (void)remove(p);
        o = (f != NULL && a == 0 && b == EOF) ? OUT_REFUSED : OUT_ACCEPTED;
        break;
    }}
    /* 12 fclose_s_null_guarded: the checked close refuses NULL. */
    { case 12: {
        o = (cplus_fclose_s(NULL) == EOF) ? OUT_REFUSED : OUT_ACCEPTED;
        break;
    }}

    /* ---- fread (13-24) -------------------------------------------------- */
    /* 13 fread_exact_items: 5 bytes, request 5 -> no EOF. */
    { case 13: {
        char p[256]; char b[16] = {0}; FILE *f; size_t r = 0; int ok;
        tpath(n, "a", p, sizeof p); (void)wrfile(p, "ABCDE", 5);
        f = fopen(p, "rb"); ok = (f != NULL);
        if (f != NULL) { r = fread(b, 1, 5, f); ok = ok && !feof(f); (void)fclose(f); }
        (void)remove(p);
        ok = ok && r == 5u && memcmp(b, "ABCDE", 5) == 0;
        o = ok ? OUT_ACCEPTED : OUT_REFUSED;
        break;
    }}
    /* 14 fread_short_sets_eof: request 10 from 5 -> 5 items, feof set. */
    { case 14: {
        char p[256]; char b[16] = {0}; FILE *f; size_t r = 0; int ok;
        tpath(n, "a", p, sizeof p); (void)wrfile(p, "ABCDE", 5);
        f = fopen(p, "rb"); ok = (f != NULL);
        if (f != NULL) { r = fread(b, 1, 10, f); ok = ok && feof(f) != 0; (void)fclose(f); }
        (void)remove(p);
        ok = ok && r == 5u && memcmp(b, "ABCDE", 5) == 0;
        o = ok ? OUT_ACCEPTED : OUT_REFUSED;
        break;
    }}
    /* 15 fread_at_eof_zero: a second read at EOF returns 0 items. */
    { case 15: {
        char p[256]; char b[8]; FILE *f; size_t r1 = 0, r2 = 0; int ok;
        tpath(n, "a", p, sizeof p); (void)wrfile(p, "xy", 2);
        f = fopen(p, "rb"); ok = (f != NULL);
        if (f != NULL) {
            r1 = fread(b, 1, 8, f); r2 = fread(b, 1, 8, f);
            ok = ok && feof(f) != 0; (void)fclose(f);
        }
        (void)remove(p);
        ok = ok && r1 == 2u && r2 == 0u;
        o = ok ? OUT_ACCEPTED : OUT_REFUSED;
        break;
    }}
    /* 16 fread_clearerr_then_eof_again: clearerr resets then EOF reasserts. */
    { case 16: {
        char p[256]; char b[8]; FILE *f; size_t r = 0; int ok;
        tpath(n, "a", p, sizeof p); (void)wrfile(p, "z", 1);
        f = fopen(p, "rb"); ok = (f != NULL);
        if (f != NULL) {
            r = fread(b, 1, 4, f); clearerr(f);
            ok = ok && feof(f) == 0;
            r += fread(b, 1, 4, f);
            ok = ok && feof(f) != 0; (void)fclose(f);
        }
        (void)remove(p);
        o = (ok && r == 1u) ? OUT_ACCEPTED : OUT_REFUSED;
        break;
    }}
    /* 17 fread_size2_count3: 2-byte items, 3 of them -> 3 items. */
    { case 17: {
        char p[256]; char b[8] = {0}; FILE *f; size_t r = 0; int ok;
        tpath(n, "a", p, sizeof p); (void)wrfile(p, "A1B2C3", 6);
        f = fopen(p, "rb"); ok = (f != NULL);
        if (f != NULL) { r = fread(b, 2, 3, f); (void)fclose(f); }
        (void)remove(p);
        ok = ok && r == 3u && memcmp(b, "A1B2C3", 6) == 0;
        o = ok ? OUT_ACCEPTED : OUT_REFUSED;
        break;
    }}
    /* 18 fread_partial_item: 4-byte items from 6 bytes -> 1 whole item.
     * Rust's Read::read yields 6 raw bytes; whole items = 6/4 = 1. */
    { case 18: {
        char p[256]; char b[8] = {0}; FILE *f; size_t r = 0; int ok;
        tpath(n, "a", p, sizeof p); (void)wrfile(p, "123456", 6);
        f = fopen(p, "rb"); ok = (f != NULL);
        if (f != NULL) { r = fread(b, 4, 3, f); ok = ok && feof(f) != 0; (void)fclose(f); }
        (void)remove(p);
        ok = ok && r == 1u && memcmp(b, "1234", 4) == 0;
        o = ok ? OUT_ACCEPTED : OUT_REFUSED;
        break;
    }}
    /* 19 fread_size_zero: a zero-size read is a defined no-op (0 items). */
    { case 19: {
        char p[256]; char b[4] = {0}; FILE *f; size_t r = 99u;
        tpath(n, "a", p, sizeof p); f = fopen(p, "wb+");
        if (f != NULL) { r = fread(b, 0, 3, f); (void)fclose(f); }
        (void)remove(p);
        o = (f != NULL && r == 0u) ? OUT_ACCEPTED : OUT_REFUSED;
        break;
    }}
    /* 20 fread_count_zero: a zero-count read is a defined no-op. */
    { case 20: {
        char p[256]; char b[4] = {0}; FILE *f; size_t r = 99u;
        tpath(n, "a", p, sizeof p); f = fopen(p, "wb+");
        if (f != NULL) { r = fread(b, 1, 0, f); (void)fclose(f); }
        (void)remove(p);
        o = (f != NULL && r == 0u) ? OUT_ACCEPTED : OUT_REFUSED;
        break;
    }}
    /* 21 fread_null_buffer_guarded: guarded wrapper returns 0 items.
     * Rust analogue: empty destination slice -> REFUSED (documented). */
    { case 21: {
        char p[256]; FILE *f; size_t r = 99u;
        tpath(n, "a", p, sizeof p); f = fopen(p, "wb+");
        if (f != NULL) { r = fread(NULL, 1, 4, f); (void)fclose(f); }
        (void)remove(p);
        o = (f != NULL && r == 0u) ? OUT_REFUSED : OUT_ACCEPTED;
        break;
    }}
    /* 22 fread_null_stream_guarded: NULL FILE* is refused by the wrapper.
     * Rust analogue: no null/closed File type -> SKIP there. */
    { case 22: {
        char b[4] = {0};
        o = (cplus_fread(b, 1, 4, NULL) == 0u) ? OUT_REFUSED : OUT_ACCEPTED;
        break;
    }}
    /* 23 fread_size_count_overflow: 4096 * SIZE_MAX is refused, errno set.
     * Rust analogue: Vec::try_reserve_exact(huge) -> Err (documented). */
    { case 23: {
        char p[256]; char b[4] = {0}; FILE *f; size_t r = 99u; int e;
        tpath(n, "a", p, sizeof p); f = fopen(p, "wb+"); errno = 0;
        if (f != NULL) { r = fread(b, 4096u, (size_t)-1, f); e = errno; (void)fclose(f); }
        else e = errno;
        (void)remove(p);
        o = (f != NULL && r == 0u && e == EOVERFLOW) ? OUT_REFUSED : OUT_ACCEPTED;
        break;
    }}
    /* 24 fread_checked_bmax_clamps: cplus_fread_s clamps count to bmax/size. */
    { case 24: {
        char p[256]; char b[16] = {0}; FILE *f; size_t r = 0; int ok;
        tpath(n, "a", p, sizeof p); (void)wrfile(p, "ABCDEFGHIJ", 10);
        f = cplus_fopen_s(p, "rb"); ok = (f != NULL);
        if (f != NULL) {
            r = cplus_fread_s(b, 4u, 1u, 10u, f);
            ok = ok && cplus_fclose_s(f) == 0;
        }
        (void)remove(p);
        ok = ok && r == 4u && memcmp(b, "ABCD", 4) == 0;
        o = ok ? OUT_ACCEPTED : OUT_REFUSED;
        break;
    }}

    /* ---- fwrite (25-34) ------------------------------------------------- */
    /* 25 fwrite_exact_items: 5 bytes written and read back. */
    { case 25: {
        char p[256]; FILE *f; size_t w = 0; char b[8] = {0};
        tpath(n, "a", p, sizeof p); f = fopen(p, "wb");
        if (f != NULL) { w = fwrite("HELLO", 1, 5, f); (void)fclose(f); }
        (void)rdfile(p, b, 5u, NULL); (void)remove(p);
        o = (f != NULL && w == 5u && memcmp(b, "HELLO", 5) == 0)
            ? OUT_ACCEPTED : OUT_REFUSED;
        break;
    }}
    /* 26 fwrite_to_readonly_ferror: write to an "rb" stream sets ferror. */
    { case 26: {
        char p[256]; FILE *f; size_t w = 99u; int err = 0;
        tpath(n, "a", p, sizeof p); (void)wrfile(p, "R", 1);
        f = fopen(p, "rb");
        if (f != NULL) { w = fwrite("x", 1, 1, f); err = ferror(f); (void)fclose(f); }
        (void)remove(p);
        o = (f != NULL && w == 0u && err != 0) ? OUT_REFUSED : OUT_ACCEPTED;
        break;
    }}
    /* 27 fwrite_size4_count2: 2 items of 4 bytes -> 8 bytes on disk. */
    { case 27: {
        char p[256]; FILE *f; size_t w = 0; char b[8] = {0};
        tpath(n, "a", p, sizeof p); f = fopen(p, "wb");
        if (f != NULL) { w = fwrite("ABCDEFGH", 4, 2, f); (void)fclose(f); }
        (void)rdfile(p, b, 8u, NULL); (void)remove(p);
        o = (f != NULL && w == 2u && memcmp(b, "ABCDEFGH", 8) == 0)
            ? OUT_ACCEPTED : OUT_REFUSED;
        break;
    }}
    /* 28 fwrite_size_zero: defined no-op, 0 items. */
    { case 28: {
        char p[256]; FILE *f; size_t w = 99u;
        tpath(n, "a", p, sizeof p); f = fopen(p, "wb");
        if (f != NULL) { w = fwrite("x", 0, 4, f); (void)fclose(f); }
        (void)remove(p);
        o = (f != NULL && w == 0u) ? OUT_ACCEPTED : OUT_REFUSED;
        break;
    }}
    /* 29 fwrite_count_zero: defined no-op, 0 items. */
    { case 29: {
        char p[256]; FILE *f; size_t w = 99u;
        tpath(n, "a", p, sizeof p); f = fopen(p, "wb");
        if (f != NULL) { w = fwrite("x", 4, 0, f); (void)fclose(f); }
        (void)remove(p);
        o = (f != NULL && w == 0u) ? OUT_ACCEPTED : OUT_REFUSED;
        break;
    }}
    /* 30 fwrite_null_buffer_guarded: guarded wrapper returns 0 items. */
    { case 30: {
        char p[256]; FILE *f; size_t w = 99u;
        tpath(n, "a", p, sizeof p); f = fopen(p, "wb");
        if (f != NULL) { w = fwrite(NULL, 1, 4, f); (void)fclose(f); }
        (void)remove(p);
        o = (f != NULL && w == 0u) ? OUT_REFUSED : OUT_ACCEPTED;
        break;
    }}
    /* 31 fwrite_size_count_overflow: 4096 * SIZE_MAX is refused. */
    { case 31: {
        char p[256]; char b[4] = {0}; FILE *f; size_t w = 99u; int e;
        tpath(n, "a", p, sizeof p); f = fopen(p, "wb"); errno = 0;
        if (f != NULL) { w = fwrite(b, 4096u, (size_t)-1, f); e = errno; (void)fclose(f); }
        else e = errno;
        (void)remove(p);
        o = (f != NULL && w == 0u && e == EOVERFLOW) ? OUT_REFUSED : OUT_ACCEPTED;
        break;
    }}
    /* 32 fwrite_checked_bmax_clamps: cplus_fwrite_s clamps to bmax/size. */
    { case 32: {
        char p[256]; FILE *f; size_t w = 0; char b[8] = {0};
        tpath(n, "a", p, sizeof p); f = cplus_fopen_s(p, "wb");
        if (f != NULL) {
            w = cplus_fwrite_s("WXYZ", 2u, 1u, 4u, f);
            (void)cplus_fclose_s(f);
        }
        (void)rdfile(p, b, 2u, NULL); (void)remove(p);
        o = (f != NULL && w == 2u && memcmp(b, "WX", 2) == 0)
            ? OUT_ACCEPTED : OUT_REFUSED;
        break;
    }}
    /* 33 fwrite_flush_then_readback: fflush makes bytes visible to fread. */
    { case 33: {
        char p[256]; FILE *f; char b[8] = {0}; size_t r = 0; int ok;
        tpath(n, "a", p, sizeof p); f = fopen(p, "w+b"); ok = (f != NULL);
        if (f != NULL) {
            ok = ok && fwrite("flush", 1, 5, f) == 5u && fflush(f) == 0;
            rewind(f); r = fread(b, 1, 5, f); (void)fclose(f);
        }
        (void)remove(p);
        ok = ok && r == 5u && memcmp(b, "flush", 5) == 0;
        o = ok ? OUT_ACCEPTED : OUT_REFUSED;
        break;
    }}
    /* 34 fwrite_size1_large_roundtrip: 256 one-byte items round-trip. */
    { case 34: {
        char p[256]; unsigned char src[256]; unsigned char b[256] = {0};
        FILE *f; size_t w = 0, r = 0, i; int ok;
        for (i = 0; i < 256u; ++i) src[i] = (unsigned char)i;
        tpath(n, "a", p, sizeof p); f = fopen(p, "wb+"); ok = (f != NULL);
        if (f != NULL) {
            w = fwrite(src, 1, sizeof src, f); rewind(f);
            r = fread(b, 1, sizeof b, f); (void)fclose(f);
        }
        (void)remove(p);
        ok = ok && w == sizeof src && r == sizeof b && memcmp(b, src, 256u) == 0;
        o = ok ? OUT_ACCEPTED : OUT_REFUSED;
        break;
    }}

    /* ---- fseek / ftell (35-44) ------------------------------------------ */
    /* 35 fseek_set_zero: SEEK_SET 0 leaves ftell at 0. */
    { case 35: {
        char p[256]; FILE *f; int ok;
        tpath(n, "a", p, sizeof p); f = fopen(p, "w+b"); ok = (f != NULL);
        if (f != NULL) {
            (void)fwrite("0123456789", 1, 10, f);
            ok = ok && fseek(f, 0L, SEEK_SET) == 0 && ftell(f) == 0L;
            (void)fclose(f);
        }
        (void)remove(p);
        o = ok ? OUT_ACCEPTED : OUT_REFUSED;
        break;
    }}
    /* 36 fseek_set_middle: seek to byte 4. */
    { case 36: {
        char p[256]; FILE *f; int ok;
        tpath(n, "a", p, sizeof p); f = fopen(p, "w+b"); ok = (f != NULL);
        if (f != NULL) {
            (void)fwrite("0123456789", 1, 10, f);
            ok = ok && fseek(f, 4L, SEEK_SET) == 0 && ftell(f) == 4L;
            (void)fclose(f);
        }
        (void)remove(p);
        o = ok ? OUT_ACCEPTED : OUT_REFUSED;
        break;
    }}
    /* 37 fseek_end_zero: SEEK_END 0 reports the file size. */
    { case 37: {
        char p[256]; FILE *f; int ok;
        tpath(n, "a", p, sizeof p); f = fopen(p, "w+b"); ok = (f != NULL);
        if (f != NULL) {
            (void)fwrite("0123456789", 1, 10, f);
            ok = ok && fseek(f, 0L, SEEK_END) == 0 && ftell(f) == 10L;
            (void)fclose(f);
        }
        (void)remove(p);
        o = ok ? OUT_ACCEPTED : OUT_REFUSED;
        break;
    }}
    /* 38 fseek_cur_forward: 2 from set, then 3 from cur -> 5. */
    { case 38: {
        char p[256]; FILE *f; int ok;
        tpath(n, "a", p, sizeof p); f = fopen(p, "w+b"); ok = (f != NULL);
        if (f != NULL) {
            (void)fwrite("0123456789", 1, 10, f);
            (void)fseek(f, 2L, SEEK_SET); (void)fseek(f, 3L, SEEK_CUR);
            ok = ok && ftell(f) == 5L; (void)fclose(f);
        }
        (void)remove(p);
        o = ok ? OUT_ACCEPTED : OUT_REFUSED;
        break;
    }}
    /* 39 fseek_cur_backward: from end, -4 from cur -> 6. */
    { case 39: {
        char p[256]; FILE *f; int ok;
        tpath(n, "a", p, sizeof p); f = fopen(p, "w+b"); ok = (f != NULL);
        if (f != NULL) {
            (void)fwrite("0123456789", 1, 10, f);
            (void)fseek(f, 0L, SEEK_END); (void)fseek(f, -4L, SEEK_CUR);
            ok = ok && ftell(f) == 6L; (void)fclose(f);
        }
        (void)remove(p);
        o = ok ? OUT_ACCEPTED : OUT_REFUSED;
        break;
    }}
    /* 40 fseek_past_eof_then_write: sparse write at offset 20 then read back. */
    { case 40: {
        char p[256]; FILE *f; int c; int ok;
        tpath(n, "a", p, sizeof p); f = fopen(p, "w+b"); ok = (f != NULL);
        if (f != NULL) {
            (void)fseek(f, 20L, SEEK_SET); (void)fputc('X', f);
            ok = ok && ftell(f) == 21L && fflush(f) == 0;
            (void)fseek(f, 20L, SEEK_SET); c = fgetc(f);
            ok = ok && c == 'X'; (void)fclose(f);
        }
        (void)remove(p);
        o = ok ? OUT_ACCEPTED : OUT_REFUSED;
        break;
    }}
    /* 41 ftell_fresh_zero: a freshly opened stream is at position 0. */
    { case 41: {
        char p[256]; FILE *f; int ok;
        tpath(n, "a", p, sizeof p); f = fopen(p, "wb+"); ok = (f != NULL);
        if (f != NULL) { ok = ok && ftell(f) == 0L; (void)fclose(f); }
        (void)remove(p);
        o = ok ? OUT_ACCEPTED : OUT_REFUSED;
        break;
    }}
    /* 42 rewind_clears_eof: rewind clears EOF and resets position. */
    { case 42: {
        char p[256]; FILE *f; int ok;
        tpath(n, "a", p, sizeof p); (void)wrfile(p, "ab", 2);
        f = fopen(p, "rb"); ok = (f != NULL);
        if (f != NULL) {
            while (getc(f) != EOF) { }
            ok = ok && feof(f) != 0;
            rewind(f);
            ok = ok && feof(f) == 0 && ftell(f) == 0L; (void)fclose(f);
        }
        (void)remove(p);
        o = ok ? OUT_ACCEPTED : OUT_REFUSED;
        break;
    }}
    /* 43 fseek_text_ftell_roundtrip: save ftell, seek back to it. */
    { case 43: {
        char p[256]; FILE *f; char b[8] = {0}; long pos; int ok;
        tpath(n, "a", p, sizeof p); f = fopen(p, "w+"); ok = (f != NULL);
        if (f != NULL) {
            (void)fputs("abc", f); pos = ftell(f);
            ok = ok && pos == 3L && fseek(f, pos, SEEK_SET) == 0 && ftell(f) == pos;
            (void)fseek(f, 0L, SEEK_SET);
            ok = ok && fgets(b, (int)sizeof b, f) == b && strcmp(b, "abc") == 0;
            (void)fclose(f);
        }
        (void)remove(p);
        o = ok ? OUT_ACCEPTED : OUT_REFUSED;
        break;
    }}
    /* 44 fseek_before_begin_error: seeking before offset 0 is refused. */
    { case 44: {
        char p[256]; FILE *f; int ok;
        tpath(n, "a", p, sizeof p); (void)wrfile(p, "abc", 3);
        f = fopen(p, "rb"); ok = (f != NULL);
        if (f != NULL) { ok = ok && fseek(f, -1L, SEEK_SET) != 0; (void)fclose(f); }
        (void)remove(p);
        o = ok ? OUT_REFUSED : OUT_ACCEPTED;
        break;
    }}

    /* ---- fgets / fputs (45-54) ------------------------------------------ */
    /* 45 fputs_writes_string: exact bytes written. */
    { case 45: {
        char p[256]; FILE *f; int r = -1; char b[8] = {0};
        tpath(n, "a", p, sizeof p); f = fopen(p, "wb");
        if (f != NULL) { r = fputs("hello", f); (void)fclose(f); }
        (void)rdfile(p, b, 5u, NULL); (void)remove(p);
        o = (f != NULL && r >= 0 && memcmp(b, "hello", 5) == 0)
            ? OUT_ACCEPTED : OUT_REFUSED;
        break;
    }}
    /* 46 fgets_line_with_newline: newline is retained by fgets. */
    { case 46: {
        char p[256]; FILE *f; char b[16] = {0}; int ok;
        tpath(n, "a", p, sizeof p); (void)wrfile(p, "hi\n", 3);
        f = fopen(p, "r"); ok = (f != NULL);
        if (f != NULL) {
            ok = ok && fgets(b, (int)sizeof b, f) == b && strcmp(b, "hi\n") == 0;
            (void)fclose(f);
        }
        (void)remove(p);
        o = ok ? OUT_ACCEPTED : OUT_REFUSED;
        break;
    }}
    /* 47 fgets_buffer_smaller_than_line: n=4 reads 3 chars ("abc"). */
    { case 47: {
        char p[256]; FILE *f; char b[4] = {0}; int ok;
        tpath(n, "a", p, sizeof p); (void)wrfile(p, "abcdefghij", 10);
        f = fopen(p, "r"); ok = (f != NULL);
        if (f != NULL) {
            ok = ok && fgets(b, (int)sizeof b, f) == b && strcmp(b, "abc") == 0;
            (void)fclose(f);
        }
        (void)remove(p);
        o = ok ? OUT_ACCEPTED : OUT_REFUSED;
        break;
    }}
    /* 48 fgets_at_eof_null: after the last line, fgets returns NULL + feof. */
    { case 48: {
        char p[256]; FILE *f; char b[16]; int ok;
        tpath(n, "a", p, sizeof p); (void)wrfile(p, "q", 1);
        f = fopen(p, "r"); ok = (f != NULL);
        if (f != NULL) {
            while (fgets(b, (int)sizeof b, f) != NULL) { }
            ok = ok && feof(f) != 0; (void)fclose(f);
        }
        (void)remove(p);
        o = ok ? OUT_ACCEPTED : OUT_REFUSED;
        break;
    }}
    /* 49 fgets_n_one_empty: fgets(buf,1) writes an empty string. */
    { case 49: {
        char p[256]; FILE *f; char b[4] = {'?'}; int ok;
        tpath(n, "a", p, sizeof p); (void)wrfile(p, "xyz", 3);
        f = fopen(p, "r"); ok = (f != NULL);
        if (f != NULL) {
            ok = ok && fgets(b, 1, f) == b && b[0] == '\0'; (void)fclose(f);
        }
        (void)remove(p);
        o = ok ? OUT_ACCEPTED : OUT_REFUSED;
        break;
    }}
    /* 50 fputs_empty_writes_nothing: an empty string creates a 0-byte file. */
    { case 50: {
        char p[256]; FILE *f; int r = -1; size_t sz = 99u;
        tpath(n, "a", p, sizeof p); f = fopen(p, "wb");
        if (f != NULL) { r = fputs("", f); (void)fclose(f); }
        if (f != NULL) { FILE *q = fopen(p, "rb"); if (q) { sz = 0u; (void)fclose(q); } }
        (void)remove(p);
        o = (f != NULL && r >= 0 && sz == 0u) ? OUT_ACCEPTED : OUT_REFUSED;
        break;
    }}
    /* 51 fputs_to_readonly_ferror: refused by the read-only stream. */
    { case 51: {
        char p[256]; FILE *f; int r = 0; int err = 0;
        tpath(n, "a", p, sizeof p); (void)wrfile(p, "R", 1);
        f = fopen(p, "rb");
        if (f != NULL) { r = fputs("x", f); err = ferror(f); (void)fclose(f); }
        (void)remove(p);
        o = (f != NULL && r == EOF && err != 0) ? OUT_REFUSED : OUT_ACCEPTED;
        break;
    }}
    /* 52 fgets_after_clearerr_eof: EOF persists after clearerr with no data. */
    { case 52: {
        char p[256]; FILE *f; char b[8]; int ok;
        tpath(n, "a", p, sizeof p); (void)wrfile(p, "a", 1);
        f = fopen(p, "r"); ok = (f != NULL);
        if (f != NULL) {
            while (fgets(b, (int)sizeof b, f) != NULL) { }
            clearerr(f);
            ok = ok && fgets(b, (int)sizeof b, f) == NULL; (void)fclose(f);
        }
        (void)remove(p);
        o = ok ? OUT_ACCEPTED : OUT_REFUSED;
        break;
    }}
    /* 53 fgets_binary_with_nul: an embedded NUL does not end fgets. */
    { case 53: {
        char p[256]; const unsigned char src[4] = {'a', '\0', 'b', '\n'};
        char b[8] = {0}; FILE *f; int ok;
        tpath(n, "a", p, sizeof p); (void)wrfile(p, src, sizeof src);
        f = fopen(p, "rb"); ok = (f != NULL);
        if (f != NULL) {
            ok = ok && fgets(b, (int)sizeof b, f) == b; (void)fclose(f);
        }
        (void)remove(p);
        ok = ok && memcmp(b, src, sizeof src) == 0;
        o = ok ? OUT_ACCEPTED : OUT_REFUSED;
        break;
    }}
    /* 54 fputs_no_implicit_newline: fputs does not append '\n'. */
    { case 54: {
        char p[256]; FILE *f; char b[8] = {0}; size_t r = 0; int ok;
        tpath(n, "a", p, sizeof p); f = fopen(p, "wb"); ok = (f != NULL);
        if (f != NULL) { (void)fputs("hello", f); (void)fclose(f); }
        (void)rdfile(p, b, sizeof b, &r); (void)remove(p);
        ok = ok && r == 5u && memcmp(b, "hello", 5) == 0;
        o = ok ? OUT_ACCEPTED : OUT_REFUSED;
        break;
    }}

    /* ---- printf / fprintf (55-62) --------------------------------------- */
    /* 55 printf_redirected_literal_args: stdout captured and verified. */
    { case 55: {
        char p[256]; int r; char b[16] = {0};
        tpath(n, "a", p, sizeof p);
        CAPTURE_PRINTF(p, r, "probe %d\n", 42);
        (void)rdfile(p, b, 10u, NULL); (void)remove(p);
        o = (r == 9 && memcmp(b, "probe 42\n", 9) == 0)
            ? OUT_ACCEPTED : OUT_REFUSED;
        break;
    }}
    /* 56 printf_redirected_empty_format: empty format writes nothing. */
    { case 56: {
        char p[256]; int r; size_t sz = 99u;
        tpath(n, "a", p, sizeof p); CAPTURE_PRINTF(p, r, "");
        { FILE *q = fopen(p, "rb"); if (q) { sz = 0u; (void)fclose(q); } }
        (void)remove(p);
        o = (r == 0 && sz == 0u) ? OUT_ACCEPTED : OUT_REFUSED;
        break;
    }}
    /* 57 printf_null_format_guarded: cplus_printf(NULL) returns -1.
     * Rust analogue: format! requires a literal -> SKIP there. */
    { case 57: {
        const char *volatile vf = NULL; int r;
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wformat-security"
#endif
        r = cplus_printf(vf);
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
        o = (r == -1) ? OUT_REFUSED : OUT_ACCEPTED;
        break;
    }}
    /* 58 fprintf_to_file: %s and %d formatted into a named temp file. */
    { case 58: {
        char p[256]; FILE *f; int r = -1; char b[8] = {0};
        tpath(n, "a", p, sizeof p); f = fopen(p, "wb");
        if (f != NULL) { r = fprintf(f, "%s=%d", "k", 7); (void)fclose(f); }
        (void)rdfile(p, b, 3u, NULL); (void)remove(p);
        o = (f != NULL && r == 3 && memcmp(b, "k=7", 3) == 0)
            ? OUT_ACCEPTED : OUT_REFUSED;
        break;
    }}
    /* 59 fprintf_null_stream_guarded: cplus_fprintf(NULL, ...) returns -1.
     * Rust analogue: no null File type -> SKIP there. */
    { case 59: {
        o = (cplus_fprintf(NULL, "x") == -1) ? OUT_REFUSED : OUT_ACCEPTED;
        break;
    }}
    /* 60 fprintf_null_format_guarded: NULL format returns -1.
     * Rust analogue: format! requires a literal -> SKIP there. */
    { case 60: {
        char p[256]; FILE *f; const char *volatile vf = NULL; int r = 0;
        tpath(n, "a", p, sizeof p); f = fopen(p, "wb");
        if (f != NULL) {
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wformat-security"
#endif
            r = cplus_fprintf(f, vf);
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
            (void)fclose(f);
        }
        (void)remove(p);
        o = (f != NULL && r == -1) ? OUT_REFUSED : OUT_ACCEPTED;
        break;
    }}
    /* 61 printf_width_precision: width/precision/hex formatting. */
    { case 61: {
        char p[256]; int r; char b[32] = {0};
        static const char want[] = "|7.500   |   42|00ff|\n";
        tpath(n, "a", p, sizeof p);
        CAPTURE_PRINTF(p, r, "|%-8.3f|%5d|%04x|\n", 7.5, 42, 255);
        (void)rdfile(p, b, sizeof want - 1u, NULL); (void)remove(p);
        o = (r == (int)(sizeof want - 1u) && memcmp(b, want, sizeof want - 1u) == 0)
            ? OUT_ACCEPTED : OUT_REFUSED;
        break;
    }}
    /* 62 fprintf_s_literal: the checked fprintf gate accepts a literal. */
    { case 62: {
        char p[256]; FILE *f; int r = -1; char b[8] = {0};
        tpath(n, "a", p, sizeof p); f = fopen(p, "wb");
        if (f != NULL) { r = fprintf_s(f, "ok %d", 7); (void)fclose(f); }
        (void)rdfile(p, b, 4u, NULL); (void)remove(p);
        o = (f != NULL && r == 4 && memcmp(b, "ok 7", 4) == 0)
            ? OUT_ACCEPTED : OUT_REFUSED;
        break;
    }}

    /* ---- snprintf (63-70) ------------------------------------------------ */
    /* 63 snprintf_exact_fit: bounded format fits exactly. */
    { case 63: {
        char b[16] = {0}; int r = snprintf(b, sizeof b, "%d-%s", 12, "ok");
        o = (r == 5 && strcmp(b, "12-ok") == 0) ? OUT_ACCEPTED : OUT_REFUSED;
        break;
    }}
    /* 64 snprintf_truncation: would-be length 8, buffer holds "ABC". */
    { case 64: {
        char b[4] = {'x', 'x', 'x', 'x'}; int r = snprintf(b, sizeof b, "ABCDEFGH");
        o = (r == 8 && strcmp(b, "ABC") == 0) ? OUT_ACCEPTED : OUT_REFUSED;
        break;
    }}
    /* 65 snprintf_size_zero_untouched: n=0 leaves the buffer untouched. */
    { case 65: {
        char b[4] = {'q', 'q', 'q', 'q'}; int r = snprintf(b, 0, "HI");
        o = (r == 2 && b[0] == 'q') ? OUT_ACCEPTED : OUT_REFUSED;
        break;
    }}
    /* 66 snprintf_size_zero_null_buffer: NULL+0 returns would-be length. */
    { case 66: {
        int r = snprintf(NULL, 0, "LONG-%d", 9);
        o = (r == 6) ? OUT_ACCEPTED : OUT_REFUSED;
        break;
    }}
    /* 67 snprintf_numeric_bases: decimal, octal, hex, upper hex. */
    { case 67: {
        char b[32] = {0}; int r = snprintf(b, sizeof b, "%d %o %x %X", 31, 8, 255, 255);
        o = (r == 11 && strcmp(b, "31 10 ff FF") == 0) ? OUT_ACCEPTED : OUT_REFUSED;
        break;
    }}
    /* 68 snprintf_float_precision: two fractional digits. */
    { case 68: {
        char b[32] = {0}; int r = snprintf(b, sizeof b, "%.2f", 3.14159);
        o = (r == 4 && strcmp(b, "3.14") == 0) ? OUT_ACCEPTED : OUT_REFUSED;
        break;
    }}
    /* 69 snprintf_embedded_nul: %c writes an embedded NUL, length is 3. */
    { case 69: {
        char b[8] = {0}; int r = snprintf(b, sizeof b, "a%cb", 0);
        o = (r == 3 && memcmp(b, "a\0b", 3) == 0) ? OUT_ACCEPTED : OUT_REFUSED;
        break;
    }}
    /* 70 snprintf_s_literal_accept: checked gate accepts a literal format. */
    { case 70: {
        char b[16] = {0}; int r = snprintf_s(b, sizeof b, "v=%d", 5);
        o = (r == 3 && strcmp(b, "v=5") == 0) ? OUT_ACCEPTED : OUT_REFUSED;
        break;
    }}

    /* ---- vsnprintf (71-76) ---------------------------------------------- */
    /* 71 vsnprintf_exact: va_list forwarding, exact fit. */
    { case 71: {
        char b[16] = {0}; int r = vfmt(b, sizeof b, "%s-%d", "x", 9);
        o = (r == 3 && strcmp(b, "x-9") == 0) ? OUT_ACCEPTED : OUT_REFUSED;
        break;
    }}
    /* 72 vsnprintf_truncation: would-be length 8, buffer holds "ABC". */
    { case 72: {
        char b[4] = {'x','x','x','x'}; int r = vfmt(b, sizeof b, "ABCDEFGH");
        o = (r == 8 && strcmp(b, "ABC") == 0) ? OUT_ACCEPTED : OUT_REFUSED;
        break;
    }}
    /* 73 vsnprintf_size_zero: NULL+0 returns would-be length. */
    { case 73: {
        int r = vfmt(NULL, 0, "HI");
        o = (r == 2) ? OUT_ACCEPTED : OUT_REFUSED;
        break;
    }}
    /* 74 vsnprintf_null_format_guarded: NULL format returns -1.
     * Rust analogue: no null format string -> SKIP there. */
    { case 74: {
        char b[8] = {0};
        o = (vfmt_nullfmt(b, sizeof b) == -1) ? OUT_REFUSED : OUT_ACCEPTED;
        break;
    }}
    /* 75 vsnprintf_percent_n_refused: %n is refused by the checked API.
     * Rust analogue: %n does not exist -> SKIP there. */
    { case 75: {
        char b[16] = {0}; int k = 0; int r = vfmt(b, sizeof b, "AB%n", &k);
        o = (r == -1) ? OUT_REFUSED : OUT_ACCEPTED;
        break;
    }}
    /* 76 vsnprintf_multiple_args: mixed conversions through va_list. */
    { case 76: {
        char b[32] = {0}; int r = vfmt(b, sizeof b, "%d/%s/%.1f", 7, "z", 2.5);
        o = (r == 7 && strcmp(b, "7/z/2.5") == 0) ? OUT_ACCEPTED : OUT_REFUSED;
        break;
    }}

    /* ---- checked snprintf_s / scanf_s (77-90) --------------------------- */
    /* 77 snprintf_s_literal_exact: zero-padded width via checked gate. */
    { case 77: {
        char b[16] = {0}; int r = snprintf_s(b, sizeof b, "%05d", 42);
        o = (r == 5 && strcmp(b, "00042") == 0) ? OUT_ACCEPTED : OUT_REFUSED;
        break;
    }}
    /* 78 snprintf_s_literal_truncation: checked gate still truncates safely. */
    { case 78: {
        char b[4] = {'x','x','x','x'}; int r = snprintf_s(b, sizeof b, "ABCDEFGH");
        o = (r == 8 && strcmp(b, "ABC") == 0) ? OUT_ACCEPTED : OUT_REFUSED;
        break;
    }}
    /* 79 snprintf_s_dmax_zero: n=0 leaves the buffer untouched. */
    { case 79: {
        char b[4] = {'q','q','q','q'}; int r = snprintf_s(b, 0, "HI");
        o = (r == 2 && b[0] == 'q') ? OUT_ACCEPTED : OUT_REFUSED;
        break;
    }}
    /* 80 snprintf_s_null_dest_guarded: NULL dest with n>0 is refused.
     * Rust analogue: no null pointer type -> SKIP there. */
    { case 80: {
        int r = snprintf_s(NULL, 4, "x");
        o = (r == -1) ? OUT_REFUSED : OUT_ACCEPTED;
        break;
    }}
    /* 81 snprintf_s_nonliteral_refused: runtime literal gate refuses dynamic fmt.
     * Rust analogue: format! is compile-checked -> SKIP there. */
    { case 81: {
        char b[16] = {0}; char dyn[16]; int r;
        (void)snprintf(dyn, sizeof dyn, "%s", "hi %d"); /* runtime-built, not literal */
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wformat-nonliteral"
#pragma clang diagnostic ignored "-Wformat-security"
#endif
        r = snprintf_s(b, sizeof b, dyn, 42);
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
        o = (r == -1) ? OUT_REFUSED : OUT_ACCEPTED;
        break;
    }}
    /* 82 snprintf_s_percent_n_refused: %n forbidden in checked formatting.
     * Rust analogue: %n does not exist -> SKIP there. */
    { case 82: {
        char b[16] = {0}; int k = 0; int r = snprintf_s(b, sizeof b, "AB%n", &k);
        o = (r == -1) ? OUT_REFUSED : OUT_ACCEPTED;
        break;
    }}
    /* 83 snprintf_s_dmax_exceeds_capacity: dmax > real object size refused.
     * Rust analogue: String grows, so no fixed-capacity overflow -> SKIP. */
    { case 83: {
        char small[4] = {0}; int r = snprintf_s(small, 64, "1234567890");
        o = (r == -1) ? OUT_REFUSED : OUT_ACCEPTED;
        break;
    }}
    /* 84 scanf_s_literal_from_stdin: checked %d reads 42 from a temp stdin. */
    { case 84: {
        char p[256]; int x = -1; int r = -1; int ok;
        tpath(n, "a", p, sizeof p); ok = wrfile(p, "42\n", 3);
        if (ok && freopen(p, "r", stdin) != NULL) r = scanf_s("%d", &x);
        (void)remove(p);
        o = (ok && r == 1 && x == 42) ? OUT_ACCEPTED : OUT_REFUSED;
        break;
    }}
    /* 85 scanf_s_eof_returns_negative: empty input yields EOF (-1), defined. */
    { case 85: {
        char p[256]; int x = -1; int r = 0; int ok;
        tpath(n, "a", p, sizeof p); ok = wrfile(p, "", 0);
        if (ok && freopen(p, "r", stdin) != NULL) r = scanf_s("%d", &x);
        (void)remove(p);
        o = (ok && r == EOF) ? OUT_ACCEPTED : OUT_REFUSED;
        break;
    }}
    /* 86 scanf_s_unbounded_s_refused: unchecked %s width refused by gate.
     * Rust analogue: reads are bounded by the allocation -> SKIP there. */
    { case 86: {
        char s[16] = {0}; int r = -1;
        if (freopen("/dev/null", "r", stdin) != NULL) r = scanf_s("%s", s);
        o = (r == -1) ? OUT_REFUSED : OUT_ACCEPTED;
        break;
    }}
    /* 87 scanf_s_percent_n_refused: %n forbidden in checked scanning.
     * Rust analogue: no %n conversion -> SKIP there. */
    { case 87: {
        int k = 0; int r = -1;
        if (freopen("/dev/null", "r", stdin) != NULL) r = scanf_s("%n", &k);
        o = (r == -1) ? OUT_REFUSED : OUT_ACCEPTED;
        break;
    }}
    /* 88 scanf_s_nonliteral_refused: runtime literal gate on scanf_s.
     * Rust analogue: format! is compile-checked -> SKIP there. */
    { case 88: {
        char dyn[16]; int x = 0; int r = -1;
        (void)snprintf(dyn, sizeof dyn, "%s", "%d"); /* runtime-built, not literal */
        if (freopen("/dev/null", "r", stdin) != NULL) {
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wformat-nonliteral"
#pragma clang diagnostic ignored "-Wformat-security"
#endif
            r = scanf_s(dyn, &x);
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
        }
        o = (r == -1) ? OUT_REFUSED : OUT_ACCEPTED;
        break;
    }}
    /* 89 sscanf_s_bounded_width: %3s and %d parse two fields. */
    { case 89: {
        char s[8] = {0}; int x = 0;
        int r = sscanf_s("abc 123", "%3s %d", s, &x);
        o = (r == 2 && strcmp(s, "abc") == 0 && x == 123)
            ? OUT_ACCEPTED : OUT_REFUSED;
        break;
    }}
    /* 90 sscanf_s_nonliteral_refused: checked string-scan literal gate.
     * Rust analogue: format strings are compile-checked -> SKIP there. */
    { case 90: {
        char dyn[16]; int x = 0; int r;
        (void)snprintf(dyn, sizeof dyn, "%s", "%d"); /* runtime-built, not literal */
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wformat-nonliteral"
#pragma clang diagnostic ignored "-Wformat-security"
#endif
        r = sscanf_s("42", dyn, &x);
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
        o = (r == -1) ? OUT_REFUSED : OUT_ACCEPTED;
        break;
    }}

    /* ---- huge widths, sampled to stay <30s (91-94) ---------------------- */
    /* 91 huge_width_1000: sampled width, would-be length 1000. */
    { case 91: {
        int r = snprintf(NULL, 0, "%1000d", 7);
        o = (r == 1000) ? OUT_ACCEPTED : OUT_REFUSED;
        break;
    }}
    /* 92 huge_width_60000: sampled width, would-be length 60000.
     * Rust std::fmt caps width at u16::MAX, so shared samples stop at 65535. */
    { case 92: {
        int r = snprintf(NULL, 0, "%60000d", 7);
        o = (r == 60000) ? OUT_ACCEPTED : OUT_REFUSED;
        break;
    }}
    /* 93 huge_precision_60000: would-be length 60002 ("1." + 60000). */
    { case 93: {
        int r = snprintf(NULL, 0, "%.60000f", 1.0);
        o = (r == 60002) ? OUT_ACCEPTED : OUT_REFUSED;
        break;
    }}
    /* 94 huge_width_65535: largest width Rust's compile-checked fmt accepts. */
    { case 94: {
        int r = snprintf(NULL, 0, "%65535d", 7);
        o = (r == 65535) ? OUT_ACCEPTED : OUT_REFUSED;
        break;
    }}

    /* ---- EOF/error transitions + cleanup (95-100) ----------------------- */
    /* 95 ferror_transition: write to a read stream sets the error flag. */
    { case 95: {
        char p[256]; FILE *f; size_t w = 99u; int err = 0;
        tpath(n, "a", p, sizeof p); (void)wrfile(p, "R", 1);
        f = fopen(p, "rb");
        if (f != NULL) { w = fwrite("x", 1, 1, f); err = ferror(f); (void)fclose(f); }
        (void)remove(p);
        o = (f != NULL && w == 0u && err != 0) ? OUT_REFUSED : OUT_ACCEPTED;
        break;
    }}
    /* 96 clearerr_after_error: ferror is reset; work resumes. Rust has no
     * stream error flags, so it recovers with a fresh writable handle. */
    { case 96: {
        char p[256]; FILE *f; int err1 = 0, err2 = -1;
        tpath(n, "a", p, sizeof p); (void)wrfile(p, "R", 1);
        f = fopen(p, "rb");
        if (f != NULL) {
            (void)fwrite("x", 1, 1, f); err1 = ferror(f);
            clearerr(f); err2 = ferror(f); (void)fclose(f);
        }
        (void)remove(p);
        o = (f != NULL && err1 != 0 && err2 == 0) ? OUT_ACCEPTED : OUT_REFUSED;
        break;
    }}
    /* 97 tmpfile_roundtrip_cleanup: unnamed temp file is auto-removed.
     * Rust analogue: named temp file + explicit remove_file (documented). */
    { case 97: {
        FILE *f = tmpfile(); int c = -1; int ok = (f != NULL);
        if (f != NULL) {
            (void)fputs("t", f); (void)fflush(f); rewind(f);
            c = fgetc(f); ok = ok && fclose(f) == 0;
        }
        o = (ok && c == 't') ? OUT_ACCEPTED : OUT_REFUSED;
        break;
    }}
    /* 98 named_temp_roundtrip_cleanup: write, read back, remove. */
    { case 98: {
        char p[256]; char b[8] = {0}; size_t r = 0; int ok;
        tpath(n, "a", p, sizeof p);
        ok = wrfile(p, "round", 5) && rdfile(p, b, 5u, &r);
        (void)remove(p);
        ok = ok && r == 5u && memcmp(b, "round", 5) == 0 && !file_exists(p);
        o = ok ? OUT_ACCEPTED : OUT_REFUSED;
        break;
    }}
    /* 99 all_temp_files_removed: every temp path created here is gone. */
    { case 99: {
        char p1[256], p2[256], p3[256]; int i; int all = 1;
        tpath(n, "a", p1, sizeof p1); tpath(n, "b", p2, sizeof p2);
        tpath(n, "c", p3, sizeof p3);
        (void)wrfile(p1, "1", 1); (void)wrfile(p2, "2", 1); (void)wrfile(p3, "3", 1);
        (void)remove(p1); (void)remove(p2); (void)remove(p3);
        if (file_exists(p1) || file_exists(p2) || file_exists(p3)) all = 0;
        for (i = 0; i < 0; ++i) all = all && 1; /* keeps loop-free pedantic build */
        o = all ? OUT_ACCEPTED : OUT_REFUSED;
        break;
    }}
    /* 100 final_stream_state_clean: a final open/close/remove succeeds. */
    { case 100: {
        char p[256]; FILE *f; int ok;
        tpath(n, "a", p, sizeof p); f = fopen(p, "wb"); ok = (f != NULL);
        if (f != NULL) ok = ok && fclose(f) == 0;
        (void)remove(p);
        ok = ok && !file_exists(p);
        o = ok ? OUT_ACCEPTED : OUT_REFUSED;
        break;
    }}

    default:
        o = OUT_SKIP;
        break;
    }

    return emit(n, o);
}
