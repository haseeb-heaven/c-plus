/* harness.c -- C99 caller for every checked cplus_v_* symbol linked from
 * build/safe/libcplus_safecrt.a (+ build/libc/libcplus_hardened.a).
 *
 * Each line printed to stdout is:  <api> <case> <token>
 * The companion vendored.rs prints the same lines using only safe Rust
 * (no unsafe / no FFI); compare.sh diffs the two streams.
 *
 * Callable signatures are copied verbatim from libc/install-include/
 * cplus_vendor_*.h. The pinned safeclib constraint handler prints chatter to
 * stdout by default, so a silent handler is installed for both the string and
 * memory families before any case runs.
 *
 * Build:
 *   /usr/bin/clang -O2 -std=c99 -Ilibc/install-include \
 *     -Ithird_party/safeclib/include \
 *     benchmarks/rust_examples/vendored_v/harness.c \
 *     build/safe/libcplus_safecrt.a build/libc/libcplus_hardened.a -o <out>
 */
#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "cplus_vendor_case.h"
#include "cplus_vendor_field.h"
#include "cplus_vendor_firstlast.h"
#include "cplus_vendor_mem.h"
#include "cplus_vendor_set.h"
#include "cplus_vendor_spn.h"
#include "cplus_vendor_stp.h"
#include "cplus_vendor_strchr.h"
#include "cplus_vendor_strl.h"
#include "cplus_vendor_strstr.h"

#define U ((size_t)-1)             /* BOS_UNKNOWN */
#define MAXSTR ((size_t)(4UL << 10)) /* RSIZE_MAX_STR = 4096 */
#define MAXMEM (MAXSTR * 4)          /* RSIZE_MAX_MEM = 16384 */

/* cplus_v__memchr_s_chk / cplus_v__memccpy_s_chk are emitted by the vendored
 * mem_group.o (verified with nm) but are intentionally absent from the public
 * cplus_vendor_mem.h, which exports only the BOS_UNKNOWN forwarding shims.
 * Declare the exact upstream BOS-aware signatures locally, as
 * tests/libc/test_vendor_mem.c does. At the pinned safeclib commit errno_t is
 * int and rsize_t is size_t. */
extern errno_t cplus_v__memchr_s_chk(const void *dest, rsize_t dmax, int ch,
                                     void **resultp, size_t destbos);
extern errno_t cplus_v__memccpy_s_chk(void *dest, rsize_t dmax,
                                      const void *src, int c, rsize_t n,
                                      size_t destbos, size_t srcbos);

/* Silent replacement for the default (chatty) constraint handlers. */
typedef void (*v_handler_t)(const char *, void *, int);
v_handler_t cplus_sc_set_str_constraint_handler_s(v_handler_t handler);
v_handler_t cplus_sc_set_mem_constraint_handler_s(v_handler_t handler);
static void quiet_handler(const char *msg, void *ptr, int err)
{
    (void)msg; (void)ptr; (void)err;
}

#define EMIT(api, case_name, tok) printf("%s %s %s\n", (api), (case_name), (tok))

/* Map the pinned safeclib error codes to stable, platform-independent tokens. */
static const char *etok(int rc)
{
    switch (rc) {
    case 0:     return "OK";
    case 400:   return "E:NULL";
    case 401:   return "E:ZERO";
    case 403:   return "E:MAX";
    case 404:   return "E:OVLP";
    case 406:   return "E:NOSPC";
    case 408:   return "E:NODIFF";
    case 409:   return "E:NOTFND";
#ifdef EOVERFLOW
    case EOVERFLOW: return "E:OVER";
#else
    case 84:    return "E:OVER";
#endif
    default:    return "E:UNKNOWN";
    }
}

/* Sign token for comparison results: locale/magnitude independent. */
static const char *stok(int v)
{
    return v < 0 ? "s=-1" : (v > 0 ? "s=1" : "s=0");
}

/* Lower-case hex of n bytes, into a caller-supplied buffer (2n+1 bytes). */
static void hexbytes(char *out, const unsigned char *p, size_t n)
{
    static const char digits[] = "0123456789abcdef";
    size_t i;
    for (i = 0; i < n; ++i) {
        out[2 * i]     = digits[p[i] >> 4];
        out[2 * i + 1] = digits[p[i] & 15];
    }
    out[2 * n] = '\0';
}

static void emit_canary(const char *api, const char *case_name,
                        const char *result, int intact)
{
    printf("%s %s %s;canary=%s\n", api, case_name, result,
           intact ? "ok" : "BAD");
}

int main(void)
{
    char tok[80], hx[64];
    char *cp;
    void *vp;
    int rc, ir;
    size_t sz;

    (void)cplus_sc_set_str_constraint_handler_s(quiet_handler);
    (void)cplus_sc_set_mem_constraint_handler_s(quiet_handler);

    /* ------------------------------------------------------------------
     * 1. cplus_v_memchr_s
     * ------------------------------------------------------------------ */
    {
        const char api[] = "cplus_v_memchr_s";
        const unsigned char hay[] = "hello world"; /* 11 bytes + NUL */
        rc = cplus_v_memchr_s(hay, 11, 'o', &vp);
        snprintf(tok, sizeof tok, "ok:i=%ld",
                 (long)((const unsigned char *)vp - hay));
        EMIT(api, "valid", rc == 0 ? tok : etok(rc));

        vp = (void *)hay;
        rc = cplus_v_memchr_s(NULL, 4, 'a', &vp);
        EMIT(api, "null", etok(rc));

        rc = cplus_v_memchr_s(hay, 0, 'a', &vp);
        EMIT(api, "zero", etok(rc));

        rc = cplus_v_memchr_s(hay, MAXMEM + 1, 'a', &vp);
        EMIT(api, "over", etok(rc));

        rc = cplus_v_memchr_s(hay, 11, 'z', &vp);
        EMIT(api, "edge", etok(rc));

        {
            unsigned char b[5] = {'a', 'b', 'c', 'd', 'Z'};
            rc = cplus_v_memchr_s(b, 4, 'd', &vp);
            snprintf(tok, sizeof tok, "ok:i=%ld",
                     (long)((unsigned char *)vp - b));
            emit_canary(api, "canary", rc == 0 ? tok : etok(rc), b[4] == 'Z');
        }
    }

    /* ------------------------------------------------------------------
     * 2. cplus_v__memchr_s_chk
     * ------------------------------------------------------------------ */
    {
        const char api[] = "cplus_v__memchr_s_chk";
        const unsigned char hay[] = "abcabc"; /* 6 bytes + NUL */
        rc = cplus_v__memchr_s_chk(hay, 6, 'c', &vp, 7);
        snprintf(tok, sizeof tok, "ok:i=%ld",
                 (long)((const unsigned char *)vp - hay));
        EMIT(api, "valid", rc == 0 ? tok : etok(rc));

        rc = cplus_v__memchr_s_chk(NULL, 4, 'a', &vp, 4);
        EMIT(api, "null", etok(rc));

        rc = cplus_v__memchr_s_chk(hay, 0, 'a', &vp, 7);
        EMIT(api, "zero", etok(rc));

        rc = cplus_v__memchr_s_chk(hay, 8, 'a', &vp, 7);
        EMIT(api, "over", etok(rc));

        rc = cplus_v__memchr_s_chk(hay, 6, 'z', &vp, 7);
        EMIT(api, "edge", etok(rc));

        {
            unsigned char b[5] = {'a', 'b', 'c', 'd', 'Z'};
            rc = cplus_v__memchr_s_chk(b, 4, 'd', &vp, 5);
            snprintf(tok, sizeof tok, "ok:i=%ld",
                     (long)((unsigned char *)vp - b));
            emit_canary(api, "canary", rc == 0 ? tok : etok(rc), b[4] == 'Z');
        }
    }

    /* ------------------------------------------------------------------
     * 3. cplus_v_memccpy_s
     * ------------------------------------------------------------------ */
    {
        const char api[] = "cplus_v_memccpy_s";
        static const unsigned char src[] = "abcXdef"; /* 7 bytes + NUL */
        unsigned char d[8];

        memset(d, 0xAA, sizeof d);
        rc = cplus_v_memccpy_s(d, 8, src, 'X', 7);
        hexbytes(hx, d, sizeof d);
        snprintf(tok, sizeof tok, "ok:%s", hx);
        EMIT(api, "valid", rc == 0 ? tok : etok(rc));

        rc = cplus_v_memccpy_s(NULL, 8, src, 'X', 7);
        EMIT(api, "null", etok(rc));

        memset(d, 0xAA, sizeof d);
        rc = cplus_v_memccpy_s(d, 0, src, 'X', 7);
        EMIT(api, "zero", etok(rc));

        memset(d, 0xAA, sizeof d);
        rc = cplus_v_memccpy_s(d, 4, "abcdefgh", 'z', 10);
        EMIT(api, "over", etok(rc));

        memset(d, 0xAA, sizeof d);
        rc = cplus_v_memccpy_s(d, 8, src, 'x', 0);
        hexbytes(hx, d, sizeof d);
        snprintf(tok, sizeof tok, "ok:%s", hx);
        EMIT(api, "edge", rc == 0 ? tok : etok(rc));

        memset(d, 0xAA, sizeof d);
        d[4] = 0x5A; /* canary outside the 4-byte work area */
        rc = cplus_v_memccpy_s(d, 4, "ab", 'b', 2);
        hexbytes(hx, d, sizeof d);
        snprintf(tok, sizeof tok, "ok:%s", hx);
        emit_canary(api, "canary", rc == 0 ? tok : etok(rc), d[4] == 0x5A);
    }

    /* ------------------------------------------------------------------
     * 4. cplus_v__memccpy_s_chk
     * ------------------------------------------------------------------ */
    {
        const char api[] = "cplus_v__memccpy_s_chk";
        static const unsigned char src[] = "abcXdef";
        unsigned char d[8];

        memset(d, 0xAA, sizeof d);
        rc = cplus_v__memccpy_s_chk(d, 8, src, 'X', 7, 8, 8);
        hexbytes(hx, d, sizeof d);
        snprintf(tok, sizeof tok, "ok:%s", hx);
        EMIT(api, "valid", rc == 0 ? tok : etok(rc));

        rc = cplus_v__memccpy_s_chk(NULL, 8, src, 'X', 7, 8, 8);
        EMIT(api, "null", etok(rc));

        memset(d, 0xAA, sizeof d);
        rc = cplus_v__memccpy_s_chk(d, 0, src, 'X', 7, 8, 8);
        EMIT(api, "zero", etok(rc));

        memset(d, 0xAA, sizeof d);
        rc = cplus_v__memccpy_s_chk(d, 9, src, 'X', 7, 8, 8);
        EMIT(api, "over", etok(rc));

        memset(d, 0xAA, sizeof d);
        rc = cplus_v__memccpy_s_chk(d, 8, src, 'x', 0, 8, 8);
        hexbytes(hx, d, sizeof d);
        snprintf(tok, sizeof tok, "ok:%s", hx);
        EMIT(api, "edge", rc == 0 ? tok : etok(rc));

        memset(d, 0xAA, sizeof d);
        d[4] = 0x5A;
        rc = cplus_v__memccpy_s_chk(d, 4, "ab", 'b', 2, 8, 8);
        hexbytes(hx, d, sizeof d);
        snprintf(tok, sizeof tok, "ok:%s", hx);
        emit_canary(api, "canary", rc == 0 ? tok : etok(rc), d[4] == 0x5A);
    }

    /* ------------------------------------------------------------------
     * 5-6. strchr_s / strrchr_s (same signature, different match)
     * ------------------------------------------------------------------ */
    {
        const char text[] = "aXbXcX"; /* 6 chars + NUL */
        char *rp = NULL;

        rc = cplus_v__strchr_s_chk(text, 6, 'X', &rp, U);
        snprintf(tok, sizeof tok, "ok:i=%ld", (long)(rp - text));
        EMIT("cplus_v__strchr_s_chk", "valid", rc == 0 ? tok : etok(rc));

        rc = cplus_v__strchr_s_chk(NULL, 6, 'X', &rp, U);
        EMIT("cplus_v__strchr_s_chk", "null", etok(rc));

        rc = cplus_v__strchr_s_chk(text, 0, 'X', &rp, U);
        EMIT("cplus_v__strchr_s_chk", "zero", etok(rc));

        rc = cplus_v__strchr_s_chk(text, 7, 'X', &rp, 6);
        EMIT("cplus_v__strchr_s_chk", "over", etok(rc));

        rc = cplus_v__strchr_s_chk(text, 6, 'z', &rp, U);
        EMIT("cplus_v__strchr_s_chk", "edge", etok(rc));

        {
            char b[5] = {'a', 'b', 'c', 'X', 'Z'};
            rc = cplus_v__strchr_s_chk(b, 4, 'X', &rp, 5);
            snprintf(tok, sizeof tok, "ok:i=%ld", (long)(rp - b));
            emit_canary("cplus_v__strchr_s_chk", "canary",
                        rc == 0 ? tok : etok(rc), b[4] == 'Z');
        }

        rc = cplus_v__strrchr_s_chk(text, 6, 'X', &rp, U);
        snprintf(tok, sizeof tok, "ok:i=%ld", (long)(rp - text));
        EMIT("cplus_v__strrchr_s_chk", "valid", rc == 0 ? tok : etok(rc));

        rc = cplus_v__strrchr_s_chk(NULL, 6, 'X', &rp, U);
        EMIT("cplus_v__strrchr_s_chk", "null", etok(rc));

        rc = cplus_v__strrchr_s_chk(text, 0, 'X', &rp, U);
        EMIT("cplus_v__strrchr_s_chk", "zero", etok(rc));

        rc = cplus_v__strrchr_s_chk(text, 7, 'X', &rp, 6);
        EMIT("cplus_v__strrchr_s_chk", "over", etok(rc));

        rc = cplus_v__strrchr_s_chk(text, 6, 'z', &rp, U);
        EMIT("cplus_v__strrchr_s_chk", "edge", etok(rc));

        {
            char b[5] = {'a', 'X', 'b', 'X', 'Z'};
            rc = cplus_v__strrchr_s_chk(b, 4, 'X', &rp, 5);
            snprintf(tok, sizeof tok, "ok:i=%ld", (long)(rp - b));
            emit_canary("cplus_v__strrchr_s_chk", "canary",
                        rc == 0 ? tok : etok(rc), b[4] == 'Z');
        }
    }

    /* ------------------------------------------------------------------
     * 7-8. strstr_s / strcasestr_s
     * ------------------------------------------------------------------ */
    {
        char *rp = NULL;

        rc = cplus_v__strstr_s_chk((char *)"hello world", 11, "world", 5,
                                   &rp, U, U);
        snprintf(tok, sizeof tok, "ok:i=%ld", (long)(rp - (char *)"hello world"));
        EMIT("cplus_v__strstr_s_chk", "valid", rc == 0 ? tok : etok(rc));

        rc = cplus_v__strstr_s_chk(NULL, 11, "world", 5, &rp, U, U);
        EMIT("cplus_v__strstr_s_chk", "null", etok(rc));

        rc = cplus_v__strstr_s_chk((char *)"hello world", 0, "world", 5,
                                   &rp, U, U);
        EMIT("cplus_v__strstr_s_chk", "zero", etok(rc));

        rc = cplus_v__strstr_s_chk((char *)"hello world", 12, "world", 5,
                                   &rp, 11, U);
        EMIT("cplus_v__strstr_s_chk", "over", etok(rc));

        rc = cplus_v__strstr_s_chk((char *)"hello world", 11, "xyz", 3,
                                   &rp, U, U);
        EMIT("cplus_v__strstr_s_chk", "edge", etok(rc));

        {
            char b[5] = {'a', 'b', 'c', 'd', 'X'};
            rc = cplus_v__strstr_s_chk(b, 4, "cd", 2, &rp, 5, U);
            snprintf(tok, sizeof tok, "ok:i=%ld", (long)(rp - b));
            emit_canary("cplus_v__strstr_s_chk", "canary",
                        rc == 0 ? tok : etok(rc), b[4] == 'X');
        }

        rc = cplus_v__strcasestr_s_chk((char *)"Hello World", 11, "world", 5,
                                       &rp, U, U);
        snprintf(tok, sizeof tok, "ok:i=%ld", (long)(rp - (char *)"Hello World"));
        EMIT("cplus_v__strcasestr_s_chk", "valid", rc == 0 ? tok : etok(rc));

        rc = cplus_v__strcasestr_s_chk(NULL, 11, "world", 5, &rp, U, U);
        EMIT("cplus_v__strcasestr_s_chk", "null", etok(rc));

        rc = cplus_v__strcasestr_s_chk((char *)"Hello World", 0, "world", 5,
                                       &rp, U, U);
        EMIT("cplus_v__strcasestr_s_chk", "zero", etok(rc));

        rc = cplus_v__strcasestr_s_chk((char *)"Hello World", 12, "world", 5,
                                       &rp, 11, U);
        EMIT("cplus_v__strcasestr_s_chk", "over", etok(rc));

        rc = cplus_v__strcasestr_s_chk((char *)"Hello World", 11, "xyz", 3,
                                       &rp, U, U);
        EMIT("cplus_v__strcasestr_s_chk", "edge", etok(rc));

        {
            char b[5] = {'a', 'b', 'C', 'D', 'x'};
            rc = cplus_v__strcasestr_s_chk(b, 4, "cd", 2, &rp, 5, U);
            snprintf(tok, sizeof tok, "ok:i=%ld", (long)(rp - b));
            emit_canary("cplus_v__strcasestr_s_chk", "canary",
                        rc == 0 ? tok : etok(rc), b[4] == 'x');
        }
    }

    /* ------------------------------------------------------------------
     * 9-10. strspn_s / strcspn_s
     * ------------------------------------------------------------------ */
    {
        sz = 999;
        rc = cplus_v__strspn_s_chk("aabbccXYZ", 9, "abc", 3, &sz, U, U);
        snprintf(tok, sizeof tok, "ok:n=%lu", (unsigned long)sz);
        EMIT("cplus_v__strspn_s_chk", "valid", rc == 0 ? tok : etok(rc));

        sz = 999;
        rc = cplus_v__strspn_s_chk(NULL, 3, "abc", 3, &sz, U, U);
        EMIT("cplus_v__strspn_s_chk", "null", etok(rc));

        sz = 999;
        rc = cplus_v__strspn_s_chk("abc", 0, "abc", 3, &sz, U, U);
        EMIT("cplus_v__strspn_s_chk", "zero", etok(rc));

        sz = 999;
        rc = cplus_v__strspn_s_chk("abc", MAXSTR + 1, "abc", 3, &sz, U, U);
        EMIT("cplus_v__strspn_s_chk", "over", etok(rc));

        sz = 999;
        rc = cplus_v__strspn_s_chk("xyzabc", 6, "abc", 3, &sz, U, U);
        snprintf(tok, sizeof tok, "ok:n=%lu", (unsigned long)sz);
        EMIT("cplus_v__strspn_s_chk", "edge", rc == 0 ? tok : etok(rc));

        {
            char b[4] = {'a', 'a', 'a', 'Z'};
            sz = 999;
            rc = cplus_v__strspn_s_chk(b, 3, "a", 1, &sz, 4, U);
            snprintf(tok, sizeof tok, "ok:n=%lu", (unsigned long)sz);
            emit_canary("cplus_v__strspn_s_chk", "canary",
                        rc == 0 ? tok : etok(rc), b[3] == 'Z');
        }

        sz = 999;
        rc = cplus_v__strcspn_s_chk("abcXYZabc", 9, "XYZ", 3, &sz, U, U);
        snprintf(tok, sizeof tok, "ok:n=%lu", (unsigned long)sz);
        EMIT("cplus_v__strcspn_s_chk", "valid", rc == 0 ? tok : etok(rc));

        sz = 999;
        rc = cplus_v__strcspn_s_chk(NULL, 3, "XYZ", 3, &sz, U, U);
        EMIT("cplus_v__strcspn_s_chk", "null", etok(rc));

        sz = 999;
        rc = cplus_v__strcspn_s_chk("abc", 0, "XYZ", 3, &sz, U, U);
        EMIT("cplus_v__strcspn_s_chk", "zero", etok(rc));

        sz = 999;
        rc = cplus_v__strcspn_s_chk("abc", MAXSTR + 1, "XYZ", 3, &sz, U, U);
        EMIT("cplus_v__strcspn_s_chk", "over", etok(rc));

        sz = 999;
        rc = cplus_v__strcspn_s_chk("XYZabc", 6, "XYZ", 3, &sz, U, U);
        snprintf(tok, sizeof tok, "ok:n=%lu", (unsigned long)sz);
        EMIT("cplus_v__strcspn_s_chk", "edge", rc == 0 ? tok : etok(rc));

        {
            char b[4] = {'a', 'b', 'c', 'Z'};
            sz = 999;
            rc = cplus_v__strcspn_s_chk(b, 3, "Z", 1, &sz, 4, U);
            snprintf(tok, sizeof tok, "ok:n=%lu", (unsigned long)sz);
            emit_canary("cplus_v__strcspn_s_chk", "canary",
                        rc == 0 ? tok : etok(rc), b[3] == 'Z');
        }
    }

    /* ------------------------------------------------------------------
     * 11. cplus_v_strlcpy
     * ------------------------------------------------------------------ */
    {
        const char api[] = "cplus_v_strlcpy";
        unsigned char d[8];

        memset(d, 0xAA, sizeof d);
        sz = cplus_v_strlcpy((char *)d, "abc", 8);
        hexbytes(hx, d, sizeof d);
        snprintf(tok, sizeof tok, "r=%lu;bytes=%s", (unsigned long)sz, hx);
        EMIT(api, "valid", tok);

        sz = cplus_v_strlcpy(NULL, "abc", 8);
        snprintf(tok, sizeof tok, "r=%lu", (unsigned long)sz);
        EMIT(api, "null", tok);

        memset(d, 0xAA, sizeof d);
        sz = cplus_v_strlcpy((char *)d, "abcd", 0);
        hexbytes(hx, d, sizeof d);
        snprintf(tok, sizeof tok, "r=%lu;bytes=%s", (unsigned long)sz, hx);
        EMIT(api, "zero", tok);

        /* one-over the exact-fit boundary: dsize == strlen(src) truncates */
        memset(d, 0xAA, sizeof d);
        sz = cplus_v_strlcpy((char *)d, "abcd", 4);
        hexbytes(hx, d, sizeof d);
        snprintf(tok, sizeof tok, "r=%lu;bytes=%s", (unsigned long)sz, hx);
        EMIT(api, "over", tok);

        memset(d, 0xAA, sizeof d);
        sz = cplus_v_strlcpy((char *)d, "", 8);
        hexbytes(hx, d, sizeof d);
        snprintf(tok, sizeof tok, "r=%lu;bytes=%s", (unsigned long)sz, hx);
        EMIT(api, "edge", tok);

        memset(d, 0xAA, sizeof d);
        d[4] = 0x5A;
        sz = cplus_v_strlcpy((char *)d, "abc", 4);
        hexbytes(hx, d, sizeof d);
        snprintf(tok, sizeof tok, "r=%lu;bytes=%s", (unsigned long)sz, hx);
        emit_canary(api, "canary", tok, d[4] == 0x5A);
    }

    /* ------------------------------------------------------------------
     * 12. cplus_v_strlcat
     * ------------------------------------------------------------------ */
    {
        const char api[] = "cplus_v_strlcat";
        unsigned char d[8];

        memset(d, 0xAA, sizeof d);
        memcpy(d, "ab\0", 3);
        sz = cplus_v_strlcat((char *)d, "cd", 8);
        hexbytes(hx, d, sizeof d);
        snprintf(tok, sizeof tok, "r=%lu;bytes=%s", (unsigned long)sz, hx);
        EMIT(api, "valid", tok);

        sz = cplus_v_strlcat(NULL, "cd", 8);
        snprintf(tok, sizeof tok, "r=%lu", (unsigned long)sz);
        EMIT(api, "null", tok);

        memset(d, 0xAA, sizeof d);
        memcpy(d, "ab\0", 3);
        sz = cplus_v_strlcat((char *)d, "cd", 0);
        hexbytes(hx, d, sizeof d);
        snprintf(tok, sizeof tok, "r=%lu;bytes=%s", (unsigned long)sz, hx);
        EMIT(api, "zero", tok);

        /* one-over exact fit: "ab" + "cdef" into dsize 5 */
        memset(d, 0xAA, sizeof d);
        memcpy(d, "ab\0", 3);
        sz = cplus_v_strlcat((char *)d, "cdef", 5);
        hexbytes(hx, d, sizeof d);
        snprintf(tok, sizeof tok, "r=%lu;bytes=%s", (unsigned long)sz, hx);
        EMIT(api, "over", tok);

        memset(d, 0xAA, sizeof d);
        memcpy(d, "ab\0", 3);
        sz = cplus_v_strlcat((char *)d, "", 8);
        hexbytes(hx, d, sizeof d);
        snprintf(tok, sizeof tok, "r=%lu;bytes=%s", (unsigned long)sz, hx);
        EMIT(api, "edge", tok);

        memset(d, 0xAA, sizeof d);
        memcpy(d, "ab\0", 3);
        d[4] = 0x5A;
        sz = cplus_v_strlcat((char *)d, "cd", 4);
        hexbytes(hx, d, sizeof d);
        snprintf(tok, sizeof tok, "r=%lu;bytes=%s", (unsigned long)sz, hx);
        emit_canary(api, "canary", tok, d[4] == 0x5A);
    }

    /* ------------------------------------------------------------------
     * 13. cplus_v__stpcpy_s_chk
     * ------------------------------------------------------------------ */
    {
        const char api[] = "cplus_v__stpcpy_s_chk";
        unsigned char d[8];

        memset(d, 0xAA, sizeof d);
        ir = -1;
        cp = cplus_v__stpcpy_s_chk((char *)d, 8, "hello", &ir, 8, U);
        hexbytes(hx, d, sizeof d);
        snprintf(tok, sizeof tok, "ok:p=%ld;bytes=%s", (long)(cp - (char *)d), hx);
        EMIT(api, "valid", ir == 0 ? tok : etok(ir));

        ir = -1;
        cp = cplus_v__stpcpy_s_chk(NULL, 8, "hello", &ir, 8, U);
        EMIT(api, "null", etok(ir));

        memset(d, 0xAA, sizeof d);
        ir = -1;
        cp = cplus_v__stpcpy_s_chk((char *)d, 0, "hello", &ir, 8, U);
        EMIT(api, "zero", etok(ir));

        memset(d, 0xAA, sizeof d);
        ir = -1;
        cp = cplus_v__stpcpy_s_chk((char *)d, 9, "hello", &ir, 8, U);
        EMIT(api, "over", etok(ir));

        memset(d, 0xAA, sizeof d);
        ir = -1;
        cp = cplus_v__stpcpy_s_chk((char *)d, 8, "", &ir, 8, U);
        hexbytes(hx, d, sizeof d);
        snprintf(tok, sizeof tok, "ok:p=%ld;bytes=%s", (long)(cp - (char *)d), hx);
        EMIT(api, "edge", ir == 0 ? tok : etok(ir));

        memset(d, 0xAA, sizeof d);
        d[6] = 0x5A;
        ir = -1;
        cp = cplus_v__stpcpy_s_chk((char *)d, 6, "abc", &ir, 8, U);
        hexbytes(hx, d, sizeof d);
        snprintf(tok, sizeof tok, "ok:p=%ld;bytes=%s", (long)(cp - (char *)d), hx);
        emit_canary(api, "canary", ir == 0 ? tok : etok(ir), d[6] == 0x5A);
    }

    /* ------------------------------------------------------------------
     * 14. cplus_v__stpncpy_s_chk
     * ------------------------------------------------------------------ */
    {
        const char api[] = "cplus_v__stpncpy_s_chk";
        unsigned char d[8];

        memset(d, 0xAA, sizeof d);
        ir = -1;
        cp = cplus_v__stpncpy_s_chk((char *)d, 8, "world", 5, &ir, 8, U);
        hexbytes(hx, d, sizeof d);
        snprintf(tok, sizeof tok, "ok:p=%ld;bytes=%s", (long)(cp - (char *)d), hx);
        EMIT(api, "valid", ir == 0 ? tok : etok(ir));

        ir = -1;
        cp = cplus_v__stpncpy_s_chk(NULL, 8, "world", 5, &ir, 8, U);
        EMIT(api, "null", etok(ir));

        memset(d, 0xAA, sizeof d);
        ir = -1;
        cp = cplus_v__stpncpy_s_chk((char *)d, 0, "world", 5, &ir, 8, U);
        EMIT(api, "zero", etok(ir));

        memset(d, 0xAA, sizeof d);
        ir = -1;
        cp = cplus_v__stpncpy_s_chk((char *)d, 8, "world", MAXSTR + 1,
                                    &ir, U, U);
        EMIT(api, "over", etok(ir));

        memset(d, 0xAA, sizeof d);
        ir = -1;
        cp = cplus_v__stpncpy_s_chk((char *)d, 8, "world", 0, &ir, 8, U);
        hexbytes(hx, d, sizeof d);
        snprintf(tok, sizeof tok, "ok:p=%ld;bytes=%s", (long)(cp - (char *)d), hx);
        EMIT(api, "edge", ir == 0 ? tok : etok(ir));

        memset(d, 0xAA, sizeof d);
        d[5] = 0x5A;
        ir = -1;
        cp = cplus_v__stpncpy_s_chk((char *)d, 5, "abcd", 3, &ir, 8, U);
        hexbytes(hx, d, sizeof d);
        snprintf(tok, sizeof tok, "ok:p=%ld;bytes=%s", (long)(cp - (char *)d), hx);
        emit_canary(api, "canary", ir == 0 ? tok : etok(ir), d[5] == 0x5A);
    }

    /* ------------------------------------------------------------------
     * 15-16. strset_s / strnset_s
     * ------------------------------------------------------------------ */
    {
        char b[8];

        memcpy(b, "AbC-XYz", 8);
        rc = cplus_v__strset_s_chk(b, 7, 'x', 8);
        EMIT("cplus_v__strset_s_chk", "valid", rc == 0 ? "ok:xxxxxxx" : etok(rc));

        rc = cplus_v__strset_s_chk(NULL, 8, 'x', U);
        EMIT("cplus_v__strset_s_chk", "null", etok(rc));

        memcpy(b, "abcdefg", 8);
        rc = cplus_v__strset_s_chk(b, 0, 'x', U);
        EMIT("cplus_v__strset_s_chk", "zero", etok(rc));

        rc = cplus_v__strset_s_chk(b, MAXSTR + 1, 'x', U);
        EMIT("cplus_v__strset_s_chk", "over", etok(rc));

        {
            char e[8] = {'A', 'B', '\0', 'C', 'D', 'E', 'F', '\0'};
            rc = cplus_v__strset_s_chk(e, 7, 'z', U);
            hexbytes(hx, (const unsigned char *)e, 7);
            snprintf(tok, sizeof tok, "ok:%s", hx);
            EMIT("cplus_v__strset_s_chk", "edge", rc == 0 ? tok : etok(rc));
        }

        {
            char e[5] = {'a', 'b', 'c', 'd', 'Z'};
            rc = cplus_v__strset_s_chk(e, 4, 'x', U);
            memcpy(hx, e, 4); hx[4] = '\0';
            snprintf(tok, sizeof tok, "ok:%s", hx);
            emit_canary("cplus_v__strset_s_chk", "canary",
                        rc == 0 ? tok : etok(rc), e[4] == 'Z');
        }

        memcpy(b, "abcdefg", 8);
        rc = cplus_v__strnset_s_chk(b, 8, 'y', 3, 8);
        EMIT("cplus_v__strnset_s_chk", "valid", rc == 0 ? "ok:yyydefg" : etok(rc));

        rc = cplus_v__strnset_s_chk(NULL, 8, 'y', 3, U);
        EMIT("cplus_v__strnset_s_chk", "null", etok(rc));

        memcpy(b, "abcdefg", 8);
        rc = cplus_v__strnset_s_chk(b, 0, 'y', 3, U);
        EMIT("cplus_v__strnset_s_chk", "zero", etok(rc));

        rc = cplus_v__strnset_s_chk(b, 8, 'y', 9, U); /* n > dmax */
        EMIT("cplus_v__strnset_s_chk", "over", etok(rc));

        memcpy(b, "abcdefg", 8);
        rc = cplus_v__strnset_s_chk(b, 8, 'y', 0, 8);
        EMIT("cplus_v__strnset_s_chk", "edge", rc == 0 ? "ok:abcdefg" : etok(rc));

        {
            char e[5] = {'a', 'b', 'c', 'd', 'Z'};
            rc = cplus_v__strnset_s_chk(e, 4, 'w', 2, U);
            memcpy(hx, e, 4); hx[4] = '\0';
            snprintf(tok, sizeof tok, "ok:%s", hx);
            emit_canary("cplus_v__strnset_s_chk", "canary",
                        rc == 0 ? tok : etok(rc), e[4] == 'Z');
        }
    }

    /* ------------------------------------------------------------------
     * 17. cplus_v__strnterminate_s_chk
     * ------------------------------------------------------------------ */
    {
        const char api[] = "cplus_v__strnterminate_s_chk";
        unsigned char d[8];

        memcpy(d, "ABCDEFGH", 8);
        sz = cplus_v__strnterminate_s_chk((char *)d, 8, U);
        hexbytes(hx, d, 8);
        snprintf(tok, sizeof tok, "r=%lu;bytes=%s", (unsigned long)sz, hx);
        EMIT(api, "valid", tok);

        sz = cplus_v__strnterminate_s_chk(NULL, 8, U);
        snprintf(tok, sizeof tok, "r=%lu", (unsigned long)sz);
        EMIT(api, "null", tok);

        memcpy(d, "ABCDEFGH", 8);
        sz = cplus_v__strnterminate_s_chk((char *)d, 0, U);
        snprintf(tok, sizeof tok, "r=%lu", (unsigned long)sz);
        EMIT(api, "zero", tok);

        memcpy(d, "ABCDEFGH", 8);
        sz = cplus_v__strnterminate_s_chk((char *)d, 9, 8);
        snprintf(tok, sizeof tok, "r=%lu", (unsigned long)sz);
        EMIT(api, "over", tok);

        {
            char e[8] = {'a', 'b', 'c', '\0', 'e', 'f', 'g', 'h'};
            sz = cplus_v__strnterminate_s_chk(e, 8, U);
            hexbytes(hx, (const unsigned char *)e, 8);
            snprintf(tok, sizeof tok, "r=%lu;bytes=%s", (unsigned long)sz, hx);
            EMIT(api, "edge", tok);
        }

        {
            char e[8] = {'a', 'b', 'c', 'd', 'A', 'A', 'A', 'Z'};
            sz = cplus_v__strnterminate_s_chk(e, 4, U);
            hexbytes(hx, (const unsigned char *)e, 8);
            snprintf(tok, sizeof tok, "r=%lu;bytes=%s", (unsigned long)sz, hx);
            emit_canary(api, "canary", tok, e[7] == 'Z');
        }
    }

    /* ------------------------------------------------------------------
     * 18-19. strtolowercase_s / strtouppercase_s
     * ------------------------------------------------------------------ */
    {
        char b[8];

        memcpy(b, "AbC-XYz", 8);
        rc = cplus_v__strtolowercase_s_chk(b, 7, 8);
        EMIT("cplus_v__strtolowercase_s_chk", "valid",
             rc == 0 ? "ok:abc-xyz" : etok(rc));

        rc = cplus_v__strtolowercase_s_chk(NULL, 7, U);
        EMIT("cplus_v__strtolowercase_s_chk", "null", etok(rc));

        memcpy(b, "AbC-XYz", 8);
        rc = cplus_v__strtolowercase_s_chk(b, 0, 8);
        EMIT("cplus_v__strtolowercase_s_chk", "zero", etok(rc));

        rc = cplus_v__strtolowercase_s_chk(b, MAXSTR + 1, U);
        EMIT("cplus_v__strtolowercase_s_chk", "over", etok(rc));

        {
            char e[8] = {'A', 'B', '\0', 'C', 'D', 'E', 'F', '\0'};
            rc = cplus_v__strtolowercase_s_chk(e, 7, U);
            hexbytes(hx, (const unsigned char *)e, 7);
            snprintf(tok, sizeof tok, "ok:%s", hx);
            EMIT("cplus_v__strtolowercase_s_chk", "edge",
                 rc == 0 ? tok : etok(rc));
        }

        {
            char e[5] = {'A', 'B', 'C', 'D', 'Z'};
            rc = cplus_v__strtolowercase_s_chk(e, 4, 5);
            memcpy(hx, e, 4); hx[4] = '\0';
            snprintf(tok, sizeof tok, "ok:%s", hx);
            emit_canary("cplus_v__strtolowercase_s_chk", "canary",
                        rc == 0 ? tok : etok(rc), e[4] == 'Z');
        }

        memcpy(b, "aBc-dEf", 8);
        rc = cplus_v__strtouppercase_s_chk(b, 7, 8);
        EMIT("cplus_v__strtouppercase_s_chk", "valid",
             rc == 0 ? "ok:ABC-DEF" : etok(rc));

        rc = cplus_v__strtouppercase_s_chk(NULL, 7, U);
        EMIT("cplus_v__strtouppercase_s_chk", "null", etok(rc));

        memcpy(b, "aBc-dEf", 8);
        rc = cplus_v__strtouppercase_s_chk(b, 0, 8);
        EMIT("cplus_v__strtouppercase_s_chk", "zero", etok(rc));

        rc = cplus_v__strtouppercase_s_chk(b, MAXSTR + 1, U);
        EMIT("cplus_v__strtouppercase_s_chk", "over", etok(rc));

        {
            char e[8] = {'a', 'b', 'c', 'd', 'e', 'f', '\0', '\0'};
            rc = cplus_v__strtouppercase_s_chk(e, 3, U);
            memcpy(hx, e, 6); hx[6] = '\0';
            snprintf(tok, sizeof tok, "ok:%s", hx);
            EMIT("cplus_v__strtouppercase_s_chk", "edge",
                 rc == 0 ? tok : etok(rc));
        }

        {
            char e[5] = {'a', 'b', 'c', 'd', 'Z'};
            rc = cplus_v__strtouppercase_s_chk(e, 4, 5);
            memcpy(hx, e, 4); hx[4] = '\0';
            snprintf(tok, sizeof tok, "ok:%s", hx);
            emit_canary("cplus_v__strtouppercase_s_chk", "canary",
                        rc == 0 ? tok : etok(rc), e[4] == 'Z');
        }
    }

    /* ------------------------------------------------------------------
     * 20-23. first/last char + first/last diff
     * ------------------------------------------------------------------ */
    {
        char *rp = NULL;
        size_t idx = 999;

        rc = cplus_v__strfirstchar_s_chk((char *)"abcabc", 6, 'c', &rp, U);
        snprintf(tok, sizeof tok, "ok:i=%ld", (long)(rp - (char *)"abcabc"));
        EMIT("cplus_v__strfirstchar_s_chk", "valid", rc == 0 ? tok : etok(rc));

        rc = cplus_v__strfirstchar_s_chk(NULL, 6, 'c', &rp, U);
        EMIT("cplus_v__strfirstchar_s_chk", "null", etok(rc));

        rc = cplus_v__strfirstchar_s_chk((char *)"abcabc", 0, 'c', &rp, U);
        EMIT("cplus_v__strfirstchar_s_chk", "zero", etok(rc));

        rc = cplus_v__strfirstchar_s_chk((char *)"abcabc", 7, 'c', &rp, 6);
        EMIT("cplus_v__strfirstchar_s_chk", "over", etok(rc));

        rc = cplus_v__strfirstchar_s_chk((char *)"abcabc", 6, 'z', &rp, U);
        EMIT("cplus_v__strfirstchar_s_chk", "edge", etok(rc));

        {
            char b[4] = {'a', 'b', 'c', 'Z'};
            rc = cplus_v__strfirstchar_s_chk(b, 3, 'c', &rp, 4);
            snprintf(tok, sizeof tok, "ok:i=%ld", (long)(rp - b));
            emit_canary("cplus_v__strfirstchar_s_chk", "canary",
                        rc == 0 ? tok : etok(rc), b[3] == 'Z');
        }

        rc = cplus_v__strlastchar_s_chk((char *)"abcabc", 6, 'a', &rp, U);
        snprintf(tok, sizeof tok, "ok:i=%ld", (long)(rp - (char *)"abcabc"));
        EMIT("cplus_v__strlastchar_s_chk", "valid", rc == 0 ? tok : etok(rc));

        rc = cplus_v__strlastchar_s_chk(NULL, 6, 'a', &rp, U);
        EMIT("cplus_v__strlastchar_s_chk", "null", etok(rc));

        rc = cplus_v__strlastchar_s_chk((char *)"abcabc", 0, 'a', &rp, U);
        EMIT("cplus_v__strlastchar_s_chk", "zero", etok(rc));

        rc = cplus_v__strlastchar_s_chk((char *)"abcabc", 7, 'a', &rp, 6);
        EMIT("cplus_v__strlastchar_s_chk", "over", etok(rc));

        rc = cplus_v__strlastchar_s_chk((char *)"abcabc", 6, 'z', &rp, U);
        EMIT("cplus_v__strlastchar_s_chk", "edge", etok(rc));

        {
            char b[4] = {'a', 'b', 'a', 'Z'};
            rc = cplus_v__strlastchar_s_chk(b, 3, 'a', &rp, 4);
            snprintf(tok, sizeof tok, "ok:i=%ld", (long)(rp - b));
            emit_canary("cplus_v__strlastchar_s_chk", "canary",
                        rc == 0 ? tok : etok(rc), b[3] == 'Z');
        }

        idx = 999;
        rc = cplus_v__strfirstdiff_s_chk("abcabc", 6, "abcXbc", &idx, U);
        snprintf(tok, sizeof tok, "ok:i=%lu", (unsigned long)idx);
        EMIT("cplus_v__strfirstdiff_s_chk", "valid", rc == 0 ? tok : etok(rc));

        rc = cplus_v__strfirstdiff_s_chk(NULL, 6, "abcXbc", &idx, U);
        EMIT("cplus_v__strfirstdiff_s_chk", "null", etok(rc));

        rc = cplus_v__strfirstdiff_s_chk("abcabc", 0, "abcXbc", &idx, U);
        EMIT("cplus_v__strfirstdiff_s_chk", "zero", etok(rc));

        rc = cplus_v__strfirstdiff_s_chk("abcabc", 7, "abcXbc", &idx, 6);
        EMIT("cplus_v__strfirstdiff_s_chk", "over", etok(rc));

        rc = cplus_v__strfirstdiff_s_chk("abcabc", 6, "abcabc", &idx, U);
        EMIT("cplus_v__strfirstdiff_s_chk", "edge", etok(rc));

        {
            char b[4] = {'a', 'b', 'c', 'Z'};
            idx = 999;
            rc = cplus_v__strfirstdiff_s_chk(b, 3, "abX", &idx, 4);
            snprintf(tok, sizeof tok, "ok:i=%lu", (unsigned long)idx);
            emit_canary("cplus_v__strfirstdiff_s_chk", "canary",
                        rc == 0 ? tok : etok(rc), b[3] == 'Z');
        }

        idx = 999;
        rc = cplus_v__strlastdiff_s_chk("abcabc", 6, "abcabX", &idx, U);
        snprintf(tok, sizeof tok, "ok:i=%lu", (unsigned long)idx);
        EMIT("cplus_v__strlastdiff_s_chk", "valid", rc == 0 ? tok : etok(rc));

        rc = cplus_v__strlastdiff_s_chk(NULL, 6, "abcabX", &idx, U);
        EMIT("cplus_v__strlastdiff_s_chk", "null", etok(rc));

        rc = cplus_v__strlastdiff_s_chk("abcabc", 0, "abcabX", &idx, U);
        EMIT("cplus_v__strlastdiff_s_chk", "zero", etok(rc));

        rc = cplus_v__strlastdiff_s_chk("abcabc", 7, "abcabX", &idx, 6);
        EMIT("cplus_v__strlastdiff_s_chk", "over", etok(rc));

        rc = cplus_v__strlastdiff_s_chk("abcabc", 6, "abcabc", &idx, U);
        EMIT("cplus_v__strlastdiff_s_chk", "edge", etok(rc));

        {
            char b[4] = {'a', 'b', 'c', 'Z'};
            idx = 999;
            rc = cplus_v__strlastdiff_s_chk(b, 3, "Xbc", &idx, 4);
            snprintf(tok, sizeof tok, "ok:i=%lu", (unsigned long)idx);
            emit_canary("cplus_v__strlastdiff_s_chk", "canary",
                        rc == 0 ? tok : etok(rc), b[3] == 'Z');
        }
    }

    /* ------------------------------------------------------------------
     * 24. cplus_v__strcmpfld_s_chk
     * ------------------------------------------------------------------ */
    {
        const char api[] = "cplus_v__strcmpfld_s_chk";

        ir = -99;
        rc = cplus_v__strcmpfld_s_chk("abcdef", 6, "abcXef", &ir, U);
        snprintf(tok, sizeof tok, "ok:%s", stok(ir));
        EMIT(api, "valid", rc == 0 ? tok : etok(rc));

        ir = 99;
        rc = cplus_v__strcmpfld_s_chk(NULL, 6, "abcXef", &ir, U);
        EMIT(api, "null", etok(rc));

        ir = 99;
        rc = cplus_v__strcmpfld_s_chk("abcdef", 0, "abcXef", &ir, U);
        EMIT(api, "zero", etok(rc));

        ir = 99;
        rc = cplus_v__strcmpfld_s_chk("abcdef", 7, "abcXef", &ir, 6);
        EMIT(api, "over", etok(rc));

        ir = -99;
        rc = cplus_v__strcmpfld_s_chk("abcdef", 6, "abcdef", &ir, U);
        snprintf(tok, sizeof tok, "ok:%s", stok(ir));
        EMIT(api, "edge", rc == 0 ? tok : etok(rc));

        {
            char b[5] = {'a', 'b', 'c', 'd', 'Z'};
            ir = -99;
            rc = cplus_v__strcmpfld_s_chk(b, 4, "abcX", &ir, 5);
            snprintf(tok, sizeof tok, "ok:%s", stok(ir));
            emit_canary(api, "canary", rc == 0 ? tok : etok(rc), b[4] == 'Z');
        }
    }

    /* ------------------------------------------------------------------
     * 25. cplus_v__strcpyfld_s_chk
     * ------------------------------------------------------------------ */
    {
        const char api[] = "cplus_v__strcpyfld_s_chk";
        unsigned char d[8];

        memset(d, 0xAA, sizeof d);
        rc = cplus_v__strcpyfld_s_chk((char *)d, 8, "HELLO", 5, 8);
        hexbytes(hx, d, 8);
        snprintf(tok, sizeof tok, "ok:%s", hx);
        EMIT(api, "valid", rc == 0 ? tok : etok(rc));

        rc = cplus_v__strcpyfld_s_chk(NULL, 8, "AB", 2, 8);
        EMIT(api, "null", etok(rc));

        memset(d, 0xAA, sizeof d);
        rc = cplus_v__strcpyfld_s_chk((char *)d, 0, "AB", 2, 0);
        EMIT(api, "zero", etok(rc));

        memset(d, 0xAA, sizeof d);
        rc = cplus_v__strcpyfld_s_chk((char *)d, 5, "AB", 2, 4);
        EMIT(api, "over", etok(rc));

        memset(d, 0xAA, sizeof d);
        rc = cplus_v__strcpyfld_s_chk((char *)d, 8, "AB", 0, 8);
        hexbytes(hx, d, 8);
        snprintf(tok, sizeof tok, "ok:%s", hx);
        EMIT(api, "edge", rc == 0 ? tok : etok(rc));

        memset(d, 0xAA, sizeof d);
        d[5] = 0x5A;
        rc = cplus_v__strcpyfld_s_chk((char *)d, 5, "AB", 2, 8);
        hexbytes(hx, d, 8);
        snprintf(tok, sizeof tok, "ok:%s", hx);
        emit_canary(api, "canary", rc == 0 ? tok : etok(rc), d[5] == 0x5A);
    }

    /* ------------------------------------------------------------------
     * 26. cplus_v__strcoll_s_chk
     * ------------------------------------------------------------------ */
    {
        const char api[] = "cplus_v__strcoll_s_chk";

        ir = -99;
        rc = cplus_v__strcoll_s_chk("abc", 3, "abd", &ir, U);
        snprintf(tok, sizeof tok, "ok:%s", stok(ir));
        EMIT(api, "valid", rc == 0 ? tok : etok(rc));

        ir = 99;
        rc = cplus_v__strcoll_s_chk(NULL, 3, "abd", &ir, U);
        EMIT(api, "null", etok(rc));

        ir = 99;
        rc = cplus_v__strcoll_s_chk("abc", 0, "abd", &ir, U);
        EMIT(api, "zero", etok(rc));

        ir = 99;
        rc = cplus_v__strcoll_s_chk("abc", 4, "abd", &ir, 3);
        EMIT(api, "over", etok(rc));

        ir = -99;
        rc = cplus_v__strcoll_s_chk("abc", 3, "abc", &ir, U);
        snprintf(tok, sizeof tok, "ok:%s", stok(ir));
        EMIT(api, "edge", rc == 0 ? tok : etok(rc));

        {
            char b[4] = {'a', 'b', '\0', 'Z'};
            ir = -99;
            rc = cplus_v__strcoll_s_chk(b, 3, "ab", &ir, 4);
            snprintf(tok, sizeof tok, "ok:%s", stok(ir));
            emit_canary(api, "canary", rc == 0 ? tok : etok(rc), b[3] == 'Z');
        }
    }

    return 0;
}
