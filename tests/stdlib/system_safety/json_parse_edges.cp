/* Task 3 Phase 4 — JSON robustness: parse / structure edges.
 *
 * Covers: empty input, malformed syntax, truncated input, duplicate keys,
 * escaped strings, Unicode escapes (BMP + surrogate pair), bounded deep
 * nesting, and bounded large arrays/objects (<4096 elements). Every malformed
 * case must produce a controlled INVALID error and never crash.
 *
 * Build (separate argv operands, as required):
 *   c+ build this.cp --strict --sanitize=none \
 *       -Ithird_party/yyjson -Lbuild/ext -lyyjson -o OUT
 * Self-checking: each check prints "<tag> <0|1>"; final line is
 * "JSON_PARSE_EDGES <checks> <failures>". Exit 0 only when failures == 0.
 *
 * Strict-mode compliance: no pointer-parameter subscripts (byte writes use
 * pointer arithmetic), no variable modulo/division, unique variable names.
 */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/json.hp"

static int jpe_checks = 0;
static int jpe_failures = 0;

static void jpe_check(const char *tag, int cond) {
    jpe_checks++;
    if (!cond)
        jpe_failures++;
    print(tag, cond ? 1 : 0);
}

/* Assert text is rejected with a well-formed INVALID/UNSUPPORTED error. */
static void jpe_bad(const char *tag, const char *text) {
    error jpe_err = error();
    json jpe_doc = json(text, &jpe_err);
    int jpe_ok = jpe_doc.valid() == 0;
    jpe_ok = jpe_ok && jpe_err.failed_now() == 1;
    jpe_ok = jpe_ok && jpe_err.category() == STDLIB_CATEGORY_INVALID;
    jpe_ok = jpe_ok && jpe_err.code() == (int)CPLUS_EXIT_UNSUPPORTED;
    jpe_ok = jpe_ok && strlen(jpe_err.what()) > 0;
    jpe_check(tag, jpe_ok);
    jpe_doc.drop();
}

/* Append one byte to a caller buffer through a pointer parameter, using
 * pointer arithmetic (never a subscript) so the strict build accepts it. */
static void jpe_put(char *out, size_t *pos, char ch) {
    *(out + *pos) = ch;
    *pos = *pos + 1;
}

/* Build "[[[...0...]]]" with jpe_depth levels of nested arrays. */
static void jpe_nested(char *out, int jpe_depth) {
    size_t jpe_pos = 0;
    int jpe_i;
    for (jpe_i = 0; jpe_i < jpe_depth; jpe_i++)
        jpe_put(out, &jpe_pos, '[');
    jpe_put(out, &jpe_pos, '0');
    for (jpe_i = 0; jpe_i < jpe_depth; jpe_i++)
        jpe_put(out, &jpe_pos, ']');
    jpe_put(out, &jpe_pos, '\0');
}

/* Build "[1,1,...,1]" with jpe_count elements. */
static void jpe_big_array(char *out, int jpe_count) {
    size_t jpe_pos = 0;
    int jpe_i;
    jpe_put(out, &jpe_pos, '[');
    for (jpe_i = 0; jpe_i < jpe_count; jpe_i++) {
        if (jpe_i > 0)
            jpe_put(out, &jpe_pos, ',');
        jpe_put(out, &jpe_pos, '1');
    }
    jpe_put(out, &jpe_pos, ']');
    jpe_put(out, &jpe_pos, '\0');
}

/* Build a flat object with jpe_count distinct keys ("k0".."kN"). */
static void jpe_big_object(char *out, int jpe_count) {
    size_t jpe_pos = 0;
    int jpe_i;
    char jpe_pair[32];
    size_t jpe_pair_len;
    jpe_put(out, &jpe_pos, '{');
    for (jpe_i = 0; jpe_i < jpe_count; jpe_i++) {
        if (jpe_i > 0)
            jpe_put(out, &jpe_pos, ',');
        snprintf(jpe_pair, sizeof(jpe_pair), "\"k%d\":1", jpe_i);
        jpe_pair_len = strlen(jpe_pair);
        __builtin_memcpy(out + jpe_pos, jpe_pair, jpe_pair_len);
        jpe_pos = jpe_pos + jpe_pair_len;
    }
    jpe_put(out, &jpe_pos, '}');
    jpe_put(out, &jpe_pos, '\0');
}

int main(void) {
    error jpe_err = error();

    /* ---- A: empty / whitespace-only input ------------------------------ */
    jpe_bad("jpe_empty_input", "");
    jpe_bad("jpe_whitespace_input", " \t\r\n  ");

    /* ---- B: malformed syntax (all must be controlled INVALID errors) --- */
    jpe_bad("jpe_unclosed_object", "{\"a\":1");
    jpe_bad("jpe_unclosed_array", "[1,2");
    jpe_bad("jpe_extra_close_brace", "{\"a\":1}}");
    jpe_bad("jpe_extra_close_bracket", "[1,2]]");
    jpe_bad("jpe_trailing_comma_object", "{\"a\":1,}");
    jpe_bad("jpe_trailing_comma_array", "[1,]");
    jpe_bad("jpe_double_comma", "[1,,2]");
    jpe_bad("jpe_missing_colon", "{\"a\" 1}");
    jpe_bad("jpe_missing_comma", "{\"a\":1 \"b\":2}");
    jpe_bad("jpe_bad_escape", "{\"a\":\"\\q\"}");
    jpe_bad("jpe_bad_unicode_hex", "{\"a\":\"\\uZZZZ\"}");
    jpe_bad("jpe_trailing_junk", "{\"a\":1} garbage");
    jpe_bad("jpe_single_quotes", "{'a':1}");
    jpe_bad("jpe_bare_word", "undefined");
    jpe_bad("jpe_leading_zero", "{\"a\":01}");
    jpe_bad("jpe_dot_number", "{\"a\":.5}");
    jpe_bad("jpe_trailing_dot", "{\"a\":1.}");
    jpe_bad("jpe_lone_minus", "{\"a\":-}");

    /* ---- C: truncated input at every proper prefix -------------------- */
    {
        const char *jpe_full = "{\"name\":\"value\",\"list\":[1,2,3]}";
        char jpe_cut[64];
        size_t jpe_full_len = strlen(jpe_full);
        size_t jpe_n;
        size_t jpe_pos;
        int jpe_all_rejected = 1;
        /* Every proper prefix of a container document is incomplete JSON and
         * must be refused, never accepted or crashed on. */
        for (jpe_n = 1; jpe_n < jpe_full_len; jpe_n++) {
            __builtin_memcpy(jpe_cut, jpe_full, jpe_n);
            jpe_pos = jpe_n;
            jpe_put(jpe_cut, &jpe_pos, '\0');
            {
                json jpe_doc = json(jpe_cut, &jpe_err);
                if (jpe_doc.valid())
                    jpe_all_rejected = 0;
                jpe_doc.drop();
            }
        }
        jpe_check("jpe_truncated_prefixes", jpe_all_rejected);
    }

    /* ---- D: duplicate keys -------------------------------------------- */
    {
        json jpe_dup = json("{\"a\":1,\"a\":2,\"a\":3}", &jpe_err);
        int jpe_ok = jpe_dup.valid() == 1;
        if (jpe_ok) {
            json_view jpe_root = jpe_dup.root();
            json_view jpe_v = jpe_root.member("a");
            int jpe_iv = -1;
            /* yyjson keeps all three entries; member() returns the first. */
            jpe_ok = jpe_ok && jpe_root.size() == 3;
            jpe_ok = jpe_ok && jpe_v.valid() == 1;
            jpe_ok = jpe_ok && jpe_v.get_int(&jpe_iv, &jpe_err) == STDLIB_OK &&
                     jpe_iv == 1;
            jpe_dup.drop();
        }
        jpe_check("jpe_duplicate_keys", jpe_ok);
    }

    /* ---- E: escaped strings ------------------------------------------- */
    {
        const char jpe_expected[] = "a\"b\\c/d\b\f\n\r\t";
        json jpe_esc = json("{\"s\":\"a\\\"b\\\\c\\/d\\b\\f\\n\\r\\t\"}",
                            &jpe_err);
        int jpe_ok = jpe_esc.valid() == 1;
        if (jpe_ok) {
            json_view jpe_root = jpe_esc.root();
            json_view jpe_v = jpe_root.member("s");
            const char *jpe_sv = (const char *)0;
            int jpe_rc = jpe_v.get_string(&jpe_sv, &jpe_err);
            jpe_ok = jpe_ok && jpe_rc == STDLIB_OK && jpe_sv != (const char *)0;
            if (jpe_ok)
                jpe_ok = strlen(jpe_sv) == sizeof(jpe_expected) - 1 &&
                         memcmp(jpe_sv, jpe_expected,
                                sizeof(jpe_expected) - 1) == 0;
            jpe_esc.drop();
        }
        jpe_check("jpe_escaped_string", jpe_ok);
    }

    /* ---- F: Unicode escapes ------------------------------------------ */
    {
        /* \u0041 'A', \u00E9 UTF-8 C3 A9, \u4F60 E4 BD A0, \u597D E5 A5 BD */
        const char jpe_expected[] = "A\xC3\xA9\xE4\xBD\xA0\xE5\xA5\xBD";
        json jpe_uni = json("{\"s\":\"\\u0041\\u00E9\\u4F60\\u597D\"}",
                            &jpe_err);
        int jpe_ok = jpe_uni.valid() == 1;
        if (jpe_ok) {
            json_view jpe_root = jpe_uni.root();
            json_view jpe_v = jpe_root.member("s");
            const char *jpe_sv = (const char *)0;
            int jpe_rc = jpe_v.get_string(&jpe_sv, &jpe_err);
            jpe_ok = jpe_ok && jpe_rc == STDLIB_OK && jpe_sv != (const char *)0;
            if (jpe_ok)
                jpe_ok = strlen(jpe_sv) == sizeof(jpe_expected) - 1 &&
                         memcmp(jpe_sv, jpe_expected,
                                sizeof(jpe_expected) - 1) == 0;
            jpe_uni.drop();
        }
        jpe_check("jpe_unicode_bmp", jpe_ok);
    }
    {
        /* Surrogate pair \uD83D\uDE00 = U+1F600 = F0 9F 98 80. */
        const char jpe_expected[] = "\xF0\x9F\x98\x80";
        json jpe_surr = json("{\"s\":\"\\uD83D\\uDE00\"}", &jpe_err);
        int jpe_ok = jpe_surr.valid() == 1;
        if (jpe_ok) {
            json_view jpe_root = jpe_surr.root();
            json_view jpe_v = jpe_root.member("s");
            const char *jpe_sv = (const char *)0;
            int jpe_rc = jpe_v.get_string(&jpe_sv, &jpe_err);
            jpe_ok = jpe_ok && jpe_rc == STDLIB_OK && jpe_sv != (const char *)0;
            if (jpe_ok)
                jpe_ok = strlen(jpe_sv) == sizeof(jpe_expected) - 1 &&
                         memcmp(jpe_sv, jpe_expected,
                                sizeof(jpe_expected) - 1) == 0;
            jpe_surr.drop();
        }
        jpe_check("jpe_unicode_surrogate_pair", jpe_ok);
    }

    /* ---- G: bounded deep nesting ------------------------------------- */
    {
        char jpe_deep[1024];
        int jpe_depth = 200;
        jpe_nested(jpe_deep, jpe_depth);
        json jpe_deep_doc = json(jpe_deep, &jpe_err);
        int jpe_ok = jpe_deep_doc.valid() == 1;
        if (jpe_ok) {
            json_view jpe_cur = jpe_deep_doc.root();
            int jpe_i;
            for (jpe_i = 0; jpe_i < jpe_depth; jpe_i++) {
                jpe_ok = jpe_ok && jpe_cur.is_array() == 1 &&
                         jpe_cur.size() == 1;
                if (!jpe_ok)
                    break;
                jpe_cur = jpe_cur.at(0);
            }
            jpe_ok = jpe_ok && jpe_cur.is_number() == 1;
            jpe_deep_doc.drop();
        }
        jpe_check("jpe_deep_nesting_200", jpe_ok);
    }

    /* ---- H: bounded large arrays / objects (<4096 elements) ---------- */
    {
        char jpe_big[8192];
        int jpe_count = 2047; /* < 4096 */
        jpe_big_array(jpe_big, jpe_count);
        json jpe_big_doc = json(jpe_big, &jpe_err);
        int jpe_ok = jpe_big_doc.valid() == 1;
        if (jpe_ok) {
            json_view jpe_root = jpe_big_doc.root();
            json_view jpe_last = jpe_root.at((size_t)(jpe_count - 1));
            int jpe_iv = -1;
            jpe_ok = jpe_ok && jpe_root.is_array() == 1 &&
                     jpe_root.size() == (size_t)jpe_count;
            jpe_ok = jpe_ok && jpe_last.valid() == 1 &&
                     jpe_last.get_int(&jpe_iv, &jpe_err) == STDLIB_OK &&
                     jpe_iv == 1;
            jpe_big_doc.drop();
        }
        jpe_check("jpe_large_array_2047", jpe_ok);
    }
    {
        char jpe_big[65536];
        int jpe_count = 2047; /* < 4096 */
        jpe_big_object(jpe_big, jpe_count);
        json jpe_big_doc = json(jpe_big, &jpe_err);
        int jpe_ok = jpe_big_doc.valid() == 1;
        if (jpe_ok) {
            json_view jpe_root = jpe_big_doc.root();
            json_view jpe_v = jpe_root.member("k2046");
            int jpe_iv = -1;
            jpe_ok = jpe_ok && jpe_root.is_object() == 1 &&
                     jpe_root.size() == (size_t)jpe_count;
            jpe_ok = jpe_ok && jpe_v.valid() == 1 &&
                     jpe_v.get_int(&jpe_iv, &jpe_err) == STDLIB_OK &&
                     jpe_iv == 1;
            jpe_big_doc.drop();
        }
        jpe_check("jpe_large_object_2047", jpe_ok);
    }

    print("JSON_PARSE_EDGES", jpe_checks, jpe_failures);
    return jpe_failures == 0 ? 0 : 1;
}
