/* Upstream-derived JSON hardening regressions for stdlib/json.hp.
 *
 * Provenance: ibireme/yyjson 0.13.0, pinned at
 * be9bd74bb3c96e00006c2075c330a6b8b90123dc (MIT, third_party/yyjson).
 * The vendored tree has no test/ directory, so the techniques below were read
 * from the upstream test suite at that exact commit:
 *   test/test_err_code.c  - INVALID_PARAMETER / EMPTY_CONTENT / UNEXPECTED_END
 *                           / JSON_STRUCTURE / INVALID_NUMBER / INVALID_STRING
 *                           / DEPTH error-code assertions, and the
 *                           "prefix of a valid JSON" truncation loop.
 *   test/test_json_reader.c - NULL / zero-length / SIZE_MAX length inputs,
 *                           read_size()/value_count() assertions, and
 *                           read_max_memory_usage / pool-allocator checks.
 *
 * Upstream's reader depth cap is a compile-time option that is 0 (disabled)
 * in the vendored archive (yyjson.h:113-117, yyjson.c:350-356), so json.hp
 * enforces the budget itself before calling yyjson.  These tests check both
 * that the upstream codes are still what we claim they are and that the C+
 * wrapper refuses the same hostile shapes.
 *
 * Build (separate operands, no combined -I/-L/-l strings):
 *   ./build/c+ build tests/stdlib/upstream_reuse/json_yyjson.cp \
 *     --strict --sanitize=none -Ithird_party/yyjson -Lbuild/ext -lyyjson \
 *     -o build/stdlib-tests/json_yyjson.strict
 *
 * Self-checking: "<tag> <0|1>" per check, final "JSON_YYJSON <checks> <failures>".
 * Exit 0 only when failures == 0.  Assumes default json.hp budgets.
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/json.hp"
#include "yyjson.h"

/* Default CPLUS_JSON_MAX_DEPTH in stdlib/json.hp; used to build an input that
 * must be refused by the default budget. */
#define JY_DEFAULT_MAX_DEPTH 16384
#define JY_NESTED64_LEN ((64 * 2) + 2)
#define JY_NESTED65_LEN ((65 * 2) + 2)
#define JY_DEEP_OK_LEN ((1024 * 2) + 2)
#define JY_DEEP_OVER_LEN ((JY_DEFAULT_MAX_DEPTH * 2) + 4)
#define JY_LARGE_LEN ((8192 * 2) + 4)

static int jy_checks = 0;
static int jy_failures = 0;

static void jy_check(const char *tag, int cond) {
    jy_checks++;
    if (!cond)
        jy_failures++;
    print(tag, cond ? 1 : 0);
}

/* ---- upstream direct-API code checks ----------------------------------- */

static void jy_upstream_err(const char *tag, const char *text, int code,
                            int pos_at_end) {
    yyjson_read_err info;
    yyjson_doc *doc;
    int ok;
    memset(&info, -1, sizeof(info));
    doc = yyjson_read_opts((char *)text, strlen(text), YYJSON_READ_NOFLAG,
                           NULL, &info);
    ok = doc == NULL && info.code == code && info.msg != NULL;
    if (pos_at_end)
        ok = ok && info.pos == strlen(text);
    jy_check(tag, ok);
    if (doc != NULL)
        yyjson_doc_free(doc);
}

/* ---- wrapper malformed-input checks ------------------------------------ */

static void jy_bad(const char *tag, const char *text) {
    error err = error();
    json doc = json(text, &err);
    int ok = doc.valid() == 0 && err.failed_now() == 1 &&
             err.category() == STDLIB_CATEGORY_INVALID &&
             err.code() == (int)CPLUS_EXIT_UNSUPPORTED &&
             strlen(err.what()) > 0;
    jy_check(tag, ok);
    doc.drop();
}

static void jy_budget_bad(const char *tag, const char *text, size_t len,
                          size_t depth, int category) {
    error err = error();
    json doc = json_parse_budget(text, len, depth, &err);
    int ok = doc.valid() == 0 && err.failed_now() == 1 &&
             err.category() == category && strlen(err.what()) > 0;
    jy_check(tag, ok);
    doc.drop();
}

static void jy_build_nested(char *out, int levels) {
    size_t pos = 0;
    int i;
    for (i = 0; i < levels; i++)
        out[pos++] = '[';
    out[pos++] = '0';
    for (i = 0; i < levels; i++)
        out[pos++] = ']';
    out[pos] = '\0';
}

int main(void) {
    error err = error();

    /* ---- 1. Upstream error-code surface (test_err_code.c) --------------- */
    {
        yyjson_read_err info;
        memset(&info, -1, sizeof(info));
        jy_check("up_null_input",
                 yyjson_read_opts(NULL, 0, 0, NULL, &info) == NULL &&
                     info.code == YYJSON_READ_ERROR_INVALID_PARAMETER);
        memset(&info, -1, sizeof(info));
        jy_check("up_zero_length",
                 yyjson_read_opts((char *)"1", 0, 0, NULL, &info) == NULL &&
                     info.code == YYJSON_READ_ERROR_INVALID_PARAMETER);
        /* yyjson.c:6407 guard; must fail before any allocation. */
        memset(&info, -1, sizeof(info));
        jy_check("up_size_max_length",
                 yyjson_read_opts((char *)"1", SIZE_MAX, 0, NULL, &info) == NULL &&
                     info.code == YYJSON_READ_ERROR_MEMORY_ALLOCATION);
    }

    /* Every truncated prefix of a scalar reports UNEXPECTED_END at its end. */
    jy_upstream_err("up_trunc_minus", "-", YYJSON_READ_ERROR_UNEXPECTED_END, 1);
    jy_upstream_err("up_trunc_exp", "123e-", YYJSON_READ_ERROR_UNEXPECTED_END, 1);
    jy_upstream_err("up_trunc_true", "tru", YYJSON_READ_ERROR_UNEXPECTED_END, 1);
    jy_upstream_err("up_trunc_false", "fals", YYJSON_READ_ERROR_UNEXPECTED_END, 1);
    jy_upstream_err("up_trunc_null", "nul", YYJSON_READ_ERROR_UNEXPECTED_END, 1);
    jy_upstream_err("up_bad_number_zero", "000", YYJSON_READ_ERROR_INVALID_NUMBER, 0);
    jy_upstream_err("up_bad_number_point", "123.e12", YYJSON_READ_ERROR_INVALID_NUMBER, 0);
    jy_upstream_err("up_bad_structure", "[1,]", YYJSON_READ_ERROR_JSON_STRUCTURE, 0);
    jy_upstream_err("up_bad_char", "[abc]", YYJSON_READ_ERROR_UNEXPECTED_CHARACTER, 0);
    jy_upstream_err("up_bad_string", "\"\\uD800\"", YYJSON_READ_ERROR_INVALID_STRING, 0);
    jy_upstream_err("up_garbage", "[1]abc", YYJSON_READ_ERROR_UNEXPECTED_CONTENT, 0);

    /* ---- 2. Wrapper regressions for the same hostile shapes ------------- */
    jy_bad("jy_malformed_object", "{not json");
    jy_bad("jy_truncated_object", "{");
    jy_bad("jy_truncated_array", "[1,2");
    jy_bad("jy_trailing_comma", "[1,]");
    jy_bad("jy_bad_escape", "{\"a\":\"\\q\"}");
    jy_bad("jy_empty", "");
    jy_bad("jy_whitespace_only", "  \n\t ");

    /* Upstream's prefix loop: every proper prefix of a valid document must be
     * rejected; the full document must parse and report both upstream metrics. */
    {
        const char *valid = "[[[{}]]]";
        size_t full = strlen(valid);
        size_t l;
        int ok = 1;
        char prefix[16];
        for (l = 1; l < full; l++) {
            unsigned int k;
            error perr = error();
            json pdoc;
            for (k = 0; k < (unsigned int)l; k++)
                prefix[k] = valid[k];
            prefix[l] = '\0';
            pdoc = json(prefix, &perr);
            if (pdoc.valid() != 0 || perr.failed_now() != 1)
                ok = 0;
            pdoc.drop();
        }
        jy_check("jy_all_truncated_prefixes", ok);
        {
            error full_err = error();
            json full_doc = json(valid, &full_err);
            jy_check("jy_valid_read_metrics",
                     full_doc.valid() == 1 && full_doc.read_size() > 0 &&
                         full_doc.value_count() > 0);
            full_doc.drop();
        }
    }

    /* ---- 3. Explicit depth budget (json_parse_budget) ------------------- */
    {
        char nested64[JY_NESTED64_LEN];
        char nested65[JY_NESTED65_LEN];
        error depth_err = error();
        json within;
        jy_build_nested(nested64, 64);
        jy_build_nested(nested65, 65);

        within = json_parse_budget(nested64, strlen(nested64), 64, &depth_err);
        jy_check("jy_depth_within_budget",
                 within.valid() == 1 && within.value_count() > 0);
        within.drop();

        jy_budget_bad("jy_depth_over_budget", nested65, strlen(nested65), 64,
                      STDLIB_CATEGORY_RANGE);
        {
            error over_err = error();
            json over = json_parse_budget(nested65, strlen(nested65), 64, &over_err);
            jy_check("jy_depth_message",
                     over.valid() == 0 && over_err.code() == (int)CPLUS_EXIT_BOUNDS &&
                         strstr(over_err.what(), "depth") != NULL);
            over.drop();
        }
    }

    /* Brackets and escaped quotes inside a string must not consume depth. */
    {
        const char *in_string = "{\"s\":\"[[[[[[[\"}";
        const char *escaped = "{\"s\":\"a\\\"[[[[\"}";
        error se = error();
        json sd = json_parse_budget(in_string, strlen(in_string), 1, &se);
        jy_check("jy_depth_ignores_strings", sd.valid() == 1);
        sd.drop();
        sd = json_parse_budget(escaped, strlen(escaped), 1, &se);
        jy_check("jy_depth_ignores_escapes", sd.valid() == 1);
        sd.drop();
    }

    /* ---- 4. Explicit length / size budget (upstream NULL/0/SIZE_MAX) ----- */
    {
        error le = error();
        json ld = json_parse_budget("1234567890", 4, 8, &le);
        int iv = -1;
        int ok = ld.valid() == 1;
        if (ok) {
            json_view root = ld.root();
            ok = root.get_int(&iv, &le) == STDLIB_OK && iv == 1234;
        }
        jy_check("jy_explicit_length", ok);
        ld.drop();

        jy_budget_bad("jy_budget_null", NULL, 1, 8, STDLIB_CATEGORY_INVALID);
        jy_budget_bad("jy_budget_zero_length", "1", 0, 8, STDLIB_CATEGORY_INVALID);
        /* Mirrors yyjson.c:6407 without allocating 2^64 bytes. */
        jy_budget_bad("jy_budget_size_max", "1", SIZE_MAX, 8, STDLIB_CATEGORY_RANGE);
    }

    /* ---- 5. Default budget: generous for real fixtures, hard on abuse --- */
    {
        char deep_ok[JY_DEEP_OK_LEN];
        char deep_over[JY_DEEP_OVER_LEN];
        jy_build_nested(deep_ok, 1024);
        jy_build_nested(deep_over, JY_DEFAULT_MAX_DEPTH + 1);
        {
            error de = error();
            json dd = json(deep_ok, &de);
            jy_check("jy_default_allows_1024", dd.valid() == 1);
            dd.drop();
        }
        {
            error de = error();
            json dd = json(deep_over, &de);
            jy_check("jy_default_rejects_over_deep",
                     dd.valid() == 0 && de.category() == STDLIB_CATEGORY_RANGE &&
                         de.code() == (int)CPLUS_EXIT_BOUNDS);
            dd.drop();
        }
    }

    /* ---- 6. Large (but in-budget) input: upstream value-count assertions - */
    {
        char large[JY_LARGE_LEN];
        size_t pos = 0;
        int i;
        error large_err = error();
        json large_doc;
        large[pos++] = '[';
        for (i = 0; i < 8192; i++) {
            if (i != 0)
                large[pos++] = ',';
            large[pos++] = '1';
        }
        large[pos++] = ']';
        large[pos] = '\0';
        large_doc = json(large, &large_err);
        /* yyjson.h:1946-1949 counts the root too: `[1,2,3]` has 4 values. */
        jy_check("jy_large_in_budget",
                 large_doc.valid() == 1 && large_doc.value_count() == 8192 + 1 &&
                     large_doc.read_size() > 0);
        large_doc.drop();
    }

    print("JSON_YYJSON", jy_checks, jy_failures);
    return jy_failures == 0 ? 0 : 1;
}
