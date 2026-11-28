/* safe_token.cp -- deterministic, bounded tokenization DEMONSTRATION.
 *
 * NOT a production parser: it tokenizes one fixed argv string with the checked
 * cplus_strtok_s / cplus_strnlen_s / cplus_strncpy_s APIs and bounded counts.
 * No files, network, clock or secrets.  Hostile oversized input, oversized
 * tokens and too many tokens are all REFUSED before a transcript is printed.
 *
 * Build (from repo root):
 *   ./build/c+ build apps/legacy_examples/utilities/security_tools/safe_token.cp \
 *     --strict --sanitize=none -I. -o apps/legacy_examples/utilities/security_tools/safe_token \
 *     -- build/safe/libcplus_safecrt.a build/libc/libcplus_hardened.a
 *
 * Usage:
 *   safe_token <text>       (1..64 printable bytes, max 4 tokens of max 16 bytes)
 *   delimiters: space, comma, semicolon
 */
#define CPLUS_SAFE_NO_UNPREFIXED_MACROS 1
#include "stdlib/safe/safe_error.h"
#include "stdlib/safe/safe_string.h"

#include <stdio.h>
#include <string.h>

#include "cplus/cplus.h"
#include "stdlib/error.hp"

#define ST_MAX_INPUT  64
#define ST_MAX_TOKENS 4
#define ST_TOKEN_CAP  16

static void st_arg(char **argv, int i, const char **out) {
    const char *v = (const char *)0;
    __builtin_memcpy(&v, argv + i, sizeof(v));
    *out = v;
}

static void st_get_ptr(char **arr, int i, char **out) {
    char *v = (char *)0;
    __builtin_memcpy(&v, arr + i, sizeof(v));
    *out = v;
}

static void st_get_len(rsize_t *arr, int i, rsize_t *out) {
    rsize_t v = 0;
    __builtin_memcpy(&v, arr + i, sizeof(v));
    *out = v;
}

static int refuse(const char *label, const char *reason, int category,
                  cplus_exit_code code) {
    error e = error();
    e.set_category(category, code, reason);
    printf("SAFETOKEN %s REFUSED code=%d cat=%d reason=%s\n",
           label, e.code(), e.category(), reason);
    fflush(stdout);
    return 2;
}

int main(int argc, char **argv) {
    char buf[ST_MAX_INPUT + 1];
    char *tokens[ST_MAX_TOKENS];
    rsize_t lengths[ST_MAX_TOKENS];
    const char *text = (const char *)0;
    rsize_t n = 0;
    rsize_t remaining = 0;
    rsize_t token_len = 0;
    char *ctx = (char *)0;
    char *tok = (char *)0;
    char *saved = (char *)0;
    int count = 0;
    int i;
    int bad_token = 0;
    int too_many = 0;

    buf[0] = '\0';
    if (argc < 2)
        return refuse("usage", "missing-input", STDLIB_CATEGORY_INVALID,
                      CPLUS_EXIT_UNSUPPORTED);
    if (argc > 2)
        return refuse("usage", "more-than-one-input", STDLIB_CATEGORY_RANGE,
                      CPLUS_EXIT_BOUNDS);
    st_arg(argv, 1, &text);
    if (text == (const char *)0)
        return refuse("usage", "null-pointer", STDLIB_CATEGORY_INVALID,
                      CPLUS_EXIT_NULL_POINTER);

    /* Bounded length query: an unterminated/too-long string is detected at
     * MAX + 1 without reading past that point. */
    n = cplus_strnlen_s(text, (rsize_t)(ST_MAX_INPUT + 1));
    if (n > (rsize_t)ST_MAX_INPUT)
        return refuse("oversize", "input-too-long", STDLIB_CATEGORY_BOUNDS,
                      CPLUS_EXIT_BOUNDS);
    if (n == 0)
        return refuse("empty", "empty-input", STDLIB_CATEGORY_INVALID,
                      CPLUS_EXIT_UNSUPPORTED);
    {
        errno_t rc = cplus_strncpy_s(buf, (rsize_t)sizeof(buf), text, n);
        if (rc != CPLUS_SAFE_OK)
            return refuse("copy", "checked-copy-failed",
                          STDLIB_CATEGORY_INVALID, CPLUS_EXIT_UNSUPPORTED);
    }
    for (i = 0; i < (int)n; i++) {
        unsigned char c = (unsigned char)buf[i];
        if (c < 0x20u || c > 0x7eu)
            return refuse("bad_byte", "non-printable-input",
                          STDLIB_CATEGORY_INVALID, CPLUS_EXIT_UNSUPPORTED);
    }

    /* Checked tokenizer: dmax is the remaining capacity and is updated by
     * cplus_strtok_s on every call.  Each token's length is itself bounded. */
    remaining = (rsize_t)sizeof(buf);
    tok = cplus_strtok_s(buf, &remaining, " ,;", &ctx);
    while (tok != (char *)0) {
        token_len = cplus_strnlen_s(tok, (rsize_t)(ST_TOKEN_CAP + 1));
        if (token_len > (rsize_t)ST_TOKEN_CAP) {
            bad_token = 1;
            break;
        }
        if (count >= ST_MAX_TOKENS) {
            too_many = 1;
            break;
        }
        tokens[count] = tok;
        lengths[count] = token_len;
        count++;
        tok = cplus_strtok_s((char *)0, &remaining, " ,;", &ctx);
    }

    if (bad_token)
        return refuse("long_token", "token-too-long", STDLIB_CATEGORY_BOUNDS,
                      CPLUS_EXIT_BOUNDS);
    if (too_many)
        return refuse("many_tokens", "too-many-tokens", STDLIB_CATEGORY_BOUNDS,
                      CPLUS_EXIT_BOUNDS);
    if (count == 0)
        return refuse("no_tokens", "no-tokens-found", STDLIB_CATEGORY_INVALID,
                      CPLUS_EXIT_UNSUPPORTED);

    /* No partial transcript is emitted for a refused input: all checks are
     * finished before the token lines are printed. */
    for (i = 0; i < count; i++) {
        st_get_ptr(tokens, i, &saved);
        st_get_len(lengths, i, &token_len);
        printf("SAFETOKEN token %d len=%u text=%s\n", i,
               (unsigned)token_len, saved);
    }
    printf("SAFETOKEN valid OK n=%d remaining=%u\n", count, (unsigned)remaining);
    printf("SAFETOKEN done\n");
    fflush(stdout);
    return 0;
}
