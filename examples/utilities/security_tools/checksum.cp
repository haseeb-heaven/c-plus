/* checksum.cp -- deterministic, bounded CRC32/FNV-1a digest utility.
 *
 * This is a DEMONSTRATION of additive checked-stdlib refusals, not a
 * production checksum tool and not a cryptographic hash.  All input comes from
 * fixed argv; no files, clock, network or secrets are used.
 *
 * Build (from repo root):
 *   ./build/c+ build examples/utilities/security_tools/checksum.cp \
 *     --strict --sanitize=none -I. -o examples/utilities/security_tools/checksum \
 *     -- build/safe/libcplus_safecrt.a build/libc/libcplus_hardened.a
 *
 * Usage:
 *   checksum <text>          (exactly one text argument, 1..32 printable bytes)
 *
 * The hostile case is an oversized argument: the *bounded* query
 * cplus_strnlen_s(arg, MAX_LEN + 1) returns MAX_LEN + 1, so the tool refuses
 * before any copy or digest work, with error code CPLUS_EXIT_BOUNDS (101).
 * A known-answer self-check must pass before any argv is processed.
 */
#define CPLUS_SAFE_NO_UNPREFIXED_MACROS 1
#include "stdlib/safe/safe_error.h"
#include "stdlib/safe/safe_string.h"

#include <stdio.h>
#include <string.h>

#include "cplus/cplus.h"
#include "stdlib/error.hp"

#define CS_MAX_LEN   32
#define CS_FNV_OFF   0x811c9dc5u
#define CS_FNV_PRIME 0x01000193u

static int g_failed = 0;

/* One deterministic protocol line; remember a mismatch in the local check. */
static void mark(const char *label, const char *got, const char *want,
                 const char *detail) {
    printf("CHECKSUM %s %s %s\n", label, got, detail);
    if (strcmp(got, want) != 0)
        g_failed = 1;
    fflush(stdout);
}

/* Refusal with a real error object from stdlib/error.hp. */
static int refuse(const char *label, const char *reason, int category,
                  cplus_exit_code code) {
    error e = error();
    e.set_category(category, code, reason);
    printf("CHECKSUM %s REFUSED code=%d cat=%d reason=%s\n",
           label, e.code(), e.category(), reason);
    fflush(stdout);
    return 2;
}

static void cs_arg(char **argv, int i, const char **out) {
    const char *v = (const char *)0;
    __builtin_memcpy(&v, argv + i, sizeof(v));
    *out = v;
}

static unsigned int cs_fnv1a(const unsigned char *p, size_t n) {
    unsigned int h = CS_FNV_OFF;
    size_t i;
    for (i = 0; i < n; i++) {
        h ^= (unsigned int)p[i];
        h *= CS_FNV_PRIME;
    }
    return h;
}

/* Bitwise CRC-32 (reflected polynomial 0xedb88320); no table, no division. */
static unsigned int cs_crc32(const unsigned char *p, size_t n) {
    unsigned int crc = 0xffffffffu;
    size_t i;
    for (i = 0; i < n; i++) {
        int k;
        crc ^= (unsigned int)p[i];
        for (k = 0; k < 8; k++)
            crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
    }
    return crc ^ 0xffffffffu;
}

int main(int argc, char **argv) {
    static const char fnv_self[] = "hello";
    static const char crc_self[] = "123456789";
    char buf[CS_MAX_LEN + 1];
    const char *text = (const char *)0;
    rsize_t n = 0;
    unsigned int fnv = 0;
    unsigned int crc = 0;
    unsigned int fnv_self_value = 0;
    unsigned int crc_self_value = 0;
    size_t i;
    char detail[160];

    /* ---- known-answer self-check: refuses to run a broken implementation -- */
    fnv_self_value = cs_fnv1a((const unsigned char *)fnv_self,
                              (size_t)(sizeof(fnv_self) - 1u));
    crc_self_value = cs_crc32((const unsigned char *)crc_self,
                              (size_t)(sizeof(crc_self) - 1u));
    if (fnv_self_value != 0x4f9f2cabu || crc_self_value != 0xcbf43926u) {
        snprintf(detail, sizeof(detail),
                 "fnv=0x%08x crc=0x%08x", fnv_self_value, crc_self_value);
        mark("selfcheck", "REFUSED", "OK", detail);
        return 2;
    }
    mark("selfcheck", "OK", "OK", "fnv=0x4f9f2cab crc=0xcbf43926");

    if (argc < 2)
        return refuse("usage", "missing-input", STDLIB_CATEGORY_INVALID,
                      CPLUS_EXIT_UNSUPPORTED);
    if (argc > 2)
        return refuse("too_many_args", "more-than-one-input",
                      STDLIB_CATEGORY_RANGE, CPLUS_EXIT_BOUNDS);
    cs_arg(argv, 1, &text);
    if (text == (const char *)0)
        return refuse("null_input", "null-pointer", STDLIB_CATEGORY_INVALID,
                      CPLUS_EXIT_NULL_POINTER);

    /* ---- hostile case: length probe is bounded to MAX_LEN + 1 -------------- */
    n = cplus_strnlen_s(text, (rsize_t)(CS_MAX_LEN + 1));
    if (n > (rsize_t)CS_MAX_LEN)
        return refuse("oversize", "input-too-long", STDLIB_CATEGORY_BOUNDS,
                      CPLUS_EXIT_BOUNDS);
    if (n == 0)
        return refuse("empty", "empty-input", STDLIB_CATEGORY_INVALID,
                      CPLUS_EXIT_UNSUPPORTED);

    buf[0] = '\0';
    {
        errno_t rc = cplus_strncpy_s(buf, (rsize_t)sizeof(buf), text, n);
        if (rc != CPLUS_SAFE_OK)
            return refuse("copy", "checked-copy-failed",
                          STDLIB_CATEGORY_INVALID, CPLUS_EXIT_UNSUPPORTED);
    }

    /* Printable ASCII only; no control bytes are accepted as digest input. */
    for (i = 0; i < (size_t)n; i++) {
        unsigned char c = (unsigned char)buf[i];
        if (c < 0x21u || c > 0x7eu)
            return refuse("bad_byte", "non-printable-input",
                          STDLIB_CATEGORY_INVALID, CPLUS_EXIT_UNSUPPORTED);
    }

    fnv = cs_fnv1a((const unsigned char *)buf, (size_t)n);
    crc = cs_crc32((const unsigned char *)buf, (size_t)n);
    snprintf(detail, sizeof(detail), "len=%u fnv=0x%08x crc=0x%08x text=%s",
             (unsigned)n, fnv, crc, buf);
    mark("valid", "OK", "OK", detail);
    printf("CHECKSUM done\n");
    fflush(stdout);
    return g_failed;
}
