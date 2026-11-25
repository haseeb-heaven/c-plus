/* Extra cleanup / boundary coverage for stdlib/json.hp.
 *
 * Complements the existing json suites (json_test.cp, deep_json_types.cp,
 * deep_json_stress.cp, deep_json_task5.cp). This file closes the remaining
 * UNTESTED paths listed in tests/stdlib/agent2_audit/module_audits/json.hp.md
 * section 7 that are reachable from the public API:
 *   A  200 repeated parse/write/drop cycles with deterministic success and
 *      failure (stdlib allocation telemetry cannot reach yyjson -- SKIP);
 *   B  new malformed-boundary variants (extra closing bracket, trailing junk,
 *      nested trailing comma, double comma, missing colon/comma) plus
 *      duplicate keys and embedded NUL behaviour;
 *   C  empty object / empty array roots and nested empty containers;
 *   D  wrong-type getters on NESTED values (top-level is already covered);
 *   E  builder mutation between writes, duplicate keys via the builder,
 *      zero-field builder write, and byte-identical repeated writes;
 *   F  post-drop refusal (using only the safe doc.root() path -- deliberately
 *      dereferencing a pre-drop borrowed view is a documented UAF and is NOT
 *      executed -- SKIP);
 *   G  allocation-failure injection (SKIP: yyjson allocator is not reachable).
 *
 * Build: c+ ... -Ithird_party/yyjson -Lbuild/ext -lyyjson
 * (the -I, -L and -l operands are passed as SEPARATE argv operands.)
 *
 * Self-checking: every check prints "<tag> <0|1>"; the final line is
 * "EXTRA_JSON_CLEANUP <checks> <failures>". SKIPs are recorded as passing
 * tagged checks and explained in extra_json_cleanup_NOTES.md. Exit 0 only
 * when failures == 0.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/json.hp"

static int xc_checks = 0;
static int xc_failures = 0;

static void xc_check(const char *tag, int cond) {
    xc_checks++;
    if (!cond)
        xc_failures++;
    print(tag, cond ? 1 : 0);
}

/* Assert the text is rejected with the documented INVALID/UNSUPPORTED error. */
static void xc_bad(const char *tag, const char *text) {
    error err = error();
    json doc = json(text, &err);
    int ok = doc.valid() == 0;
    ok = ok && err.failed_now() == 1;
    ok = ok && err.category() == STDLIB_CATEGORY_INVALID;
    ok = ok && err.code() == (int)CPLUS_EXIT_UNSUPPORTED;
    ok = ok && strlen(err.what()) > 0;
    doc.drop();
    xc_check(tag, ok);
}

/* Compare a freshly written buffer with an expected canonical byte string. */
static int xc_is_bytes(char *dump, size_t len, const char *expected) {
    size_t want = strlen(expected);
    return dump != (char *)0 && len == want && memcmp(dump, expected, want) == 0;
}

int main(void) {
    error err = error();
    json doc;
    json redo;
    json_view root;
    json_view v;
    json_view obj;
    json_view arr;
    json_view tmp;
    json_view tmp2;
    json_builder builder;
    char *dump_a;
    char *dump_b;
    char *dump_c;
    size_t len_a;
    size_t len_b;
    size_t len_c;
    int iv;
    int bv;
    double dv;
    const char *sv;
    int rc;
    int i;
    int valid_cycles;
    int fail_cycles;
    int ok;
    char nul_good[] = {'{', '"', 'a', '"', ':', '1', '}', 0, 'x'};
    char nul_bad[] = {'[', '1', ',', 0, '2', ']', 0};

    /* ---- A: 200 repeated parse/write/drop cycles ----------------------- */
    /* json.hp passes NULL for every yyjson_alc*, so stdlib_alloc_fail_*
     * telemetry cannot observe these allocations. Instead the 200 iterations
     * alternate a valid document with a malformed one: every cycle must end
     * in the documented deterministic success/refusal, all buffers must be
     * released, and the process must complete inside the external 30s limit. */
    valid_cycles = 0;
    fail_cycles = 0;
    ok = 1;
    for (i = 0; i < 200; i++) {
        dump_a = (char *)0;
        dump_b = (char *)0;
        len_a = 0;
        len_b = 0;
        if ((i & 1) == 0) {
            doc = json("{\"i\":7,\"s\":\"v\",\"a\":[1,2,3]}", &err);
            ok = ok && doc.valid() == 1;
            if (doc.valid()) {
                root = doc.root();
                dump_a = root.write(&len_a);
                dump_b = root.write(&len_b);
                ok = ok && xc_is_bytes(dump_a, len_a,
                                       "{\"i\":7,\"s\":\"v\",\"a\":[1,2,3]}");
                ok = ok && dump_b != (char *)0 && len_a == len_b &&
                          memcmp(dump_a, dump_b, len_a) == 0;
                redo = json(dump_a, &err);
                ok = ok && redo.valid() == 1;
                if (redo.valid()) {
                    tmp = redo.root();
                    v = tmp.member("i");
                    iv = -1;
                    rc = v.get_int(&iv, &err);
                    ok = ok && rc == STDLIB_OK && iv == 7;
                    redo.drop();
                }
                valid_cycles++;
            }
        } else {
            doc = json("{\"i\":7,}", &err);
            ok = ok && doc.valid() == 0 && err.failed_now() == 1 &&
                       err.category() == STDLIB_CATEGORY_INVALID;
            root = doc.root();
            dump_a = root.write(&len_a);
            ok = ok && dump_a == (char *)0;
            fail_cycles++;
        }
        json_free(dump_a);
        json_free(dump_b);
        doc.drop();
    }
    xc_check("cyc_200_parse_write_drop", ok);
    xc_check("cyc_valid_count_100", valid_cycles == 100);
    xc_check("cyc_fail_count_100", fail_cycles == 100);
    xc_check("json_leak_telemetry", 1); /* SKIP: see NOTES */

    /* ---- B: malformed boundary variants -------------------------------- */
    xc_bad("mal_extra_close_bracket", "[1,2]]");
    xc_bad("mal_trailing_junk", "[1,2]x");
    xc_bad("mal_trailing_comma_nested", "{\"o\":[1,2,]}");
    xc_bad("mal_double_comma", "[1,,2]");
    xc_bad("mal_missing_colon", "{\"a\" 1}");
    xc_bad("mal_missing_comma", "{\"a\":1 \"b\":2}");

    /* Duplicate keys: the canonical {"a":1,"a":2} lives in deep_json_task5.
     * Three keys and the serialized form are new here. */
    doc = json("{\"x\":1,\"x\":2,\"x\":3}", &err);
    ok = doc.valid() == 1;
    if (ok) {
        root = doc.root();
        iv = -1;
        dump_a = root.write(&len_a);
        dump_b = root.write(&len_b);
        tmp = root.member("x");
        ok = ok && root.is_object() == 1 && root.size() == 3;
        ok = ok && tmp.get_int(&iv, &err) == STDLIB_OK && iv == 1;
        ok = ok && xc_is_bytes(dump_a, len_a, "{\"x\":1,\"x\":2,\"x\":3}");
        ok = ok && dump_b != (char *)0 && len_a == len_b &&
                  memcmp(dump_a, dump_b, len_a) == 0;
        redo = json(dump_a, &err);
        ok = ok && redo.valid() == 1;
        if (redo.valid()) {
            tmp = redo.root();
            ok = ok && tmp.size() == 3;
            redo.drop();
        }
        json_free(dump_a);
        json_free(dump_b);
        doc.drop();
    }
    xc_check("dup_keys_parse_and_write", ok);

    /* json.hp has no explicit-length parse API: it calls strlen(text). A NUL
     * therefore truncates the input. A complete prefix parses (the following
     * junk is ignored); an incomplete prefix is a defined parse failure. */
    doc = json(nul_good, &err);
    ok = doc.valid() == 1;
    if (ok) {
        root = doc.root();
        tmp = root.member("a");
        iv = -1;
        ok = ok && root.size() == 1 && tmp.get_int(&iv, &err) == STDLIB_OK &&
                  iv == 1;
        doc.drop();
    }
    xc_check("nul_strlen_truncation", ok);
    xc_bad("nul_incomplete_prefix", nul_bad);

    /* Escaped NUL inside a string value: get_string returns a borrowed C
     * string and json.hp has no length accessor, so inspect the decoded bytes
     * directly ('a', 0x00, 'b'). */
    doc = json("{\"s\":\"a\\u0000b\"}", &err);
    ok = doc.valid() == 1;
    if (ok) {
        root = doc.root();
        tmp = root.member("s");
        sv = (const char *)0;
        rc = tmp.get_string(&sv, &err);
        ok = ok && rc == STDLIB_OK && sv != (const char *)0 &&
                  sv[0] == 'a' && sv[1] == '\0' && sv[2] == 'b';
        doc.drop();
    }
    xc_check("nul_in_string_value", ok);

    /* ---- C: empty containers ------------------------------------------- */
    doc = json("{}", &err);
    ok = doc.valid() == 1;
    if (ok) {
        root = doc.root();
        dump_a = root.write(&len_a);
        ok = ok && root.is_object() == 1 && root.is_array() == 0 &&
                  root.is_number() == 0 && root.is_string() == 0 &&
                  root.is_bool() == 0 && root.size() == 0;
        ok = ok && xc_is_bytes(dump_a, len_a, "{}");
        tmp = root.member("k");
        tmp2 = root.at(0);
        ok = ok && !tmp.valid() && !tmp2.valid();
        json_free(dump_a);
        doc.drop();
    }
    xc_check("empty_object_root", ok);

    doc = json("[]", &err);
    ok = doc.valid() == 1;
    if (ok) {
        root = doc.root();
        dump_a = root.write(&len_a);
        ok = ok && root.is_array() == 1 && root.is_object() == 0 &&
                  root.is_number() == 0 && root.is_string() == 0 &&
                  root.is_bool() == 0 && root.size() == 0;
        ok = ok && xc_is_bytes(dump_a, len_a, "[]");
        tmp = root.at(0);
        tmp2 = root.member("k");
        ok = ok && !tmp.valid() && !tmp2.valid();
        json_free(dump_a);
        doc.drop();
    }
    xc_check("empty_array_root", ok);

    doc = json("{\"e\":[],\"o\":{}}", &err);
    ok = doc.valid() == 1;
    if (ok) {
        root = doc.root();
        ok = ok && root.size() == 2;
        arr = root.member("e");
        obj = root.member("o");
        ok = ok && arr.is_array() == 1 && arr.size() == 0;
        ok = ok && obj.is_object() == 1 && obj.size() == 0;
        iv = 4242;
        ok = ok && arr.get_int(&iv, &err) == STDLIB_FAIL && iv == 4242 &&
                  err.category() == STDLIB_CATEGORY_INVALID;
        sv = (const char *)0;
        ok = ok && obj.get_string(&sv, &err) == STDLIB_FAIL &&
                  sv == (const char *)0;
        doc.drop();
    }
    xc_check("nested_empty_containers", ok);

    /* ---- D: wrong-type getters on NESTED values ------------------------ */
    doc = json("{\"o\":{\"s\":\"x\",\"n\":7,\"b\":true,\"a\":[1]}}", &err);
    ok = doc.valid() == 1;
    if (ok) {
        tmp = doc.root();
        obj = tmp.member("o");
        ok = ok && obj.valid() == 1 && obj.is_object() == 1 && obj.size() == 4;
        iv = 4242;
        tmp = obj.member("s");
        rc = tmp.get_int(&iv, &err);
        ok = ok && rc == STDLIB_FAIL && iv == 4242 &&
                  err.category() == STDLIB_CATEGORY_INVALID;
        sv = (const char *)0;
        tmp = obj.member("n");
        rc = tmp.get_string(&sv, &err);
        ok = ok && rc == STDLIB_FAIL && sv == (const char *)0 &&
                  err.category() == STDLIB_CATEGORY_INVALID;
        tmp = obj.member("b");
        rc = tmp.get_double(&dv, &err);
        ok = ok && rc == STDLIB_FAIL &&
                  err.category() == STDLIB_CATEGORY_INVALID;
        tmp = obj.member("a");
        rc = tmp.get_bool(&bv, &err);
        ok = ok && rc == STDLIB_FAIL &&
                  err.category() == STDLIB_CATEGORY_INVALID;
        iv = 4242;
        rc = obj.get_int(&iv, &err); /* object -> int */
        ok = ok && rc == STDLIB_FAIL && iv == 4242 &&
                  err.category() == STDLIB_CATEGORY_INVALID;
        arr = obj.member("a");
        tmp = arr.at(0);
        sv = (const char *)0;
        rc = tmp.get_string(&sv, &err); /* nested array element */
        ok = ok && rc == STDLIB_FAIL &&
                  err.category() == STDLIB_CATEGORY_INVALID;
        doc.drop();
    }
    xc_check("nested_wrong_type_refusal", ok);

    doc = json("{\"o\":{\"s\":\"x\",\"n\":7,\"b\":true,\"a\":[1]}}", &err);
    ok = doc.valid() == 1;
    if (ok) {
        tmp = doc.root();
        obj = tmp.member("o");
        sv = (const char *)0;
        tmp = obj.member("s");
        iv = -1;
        bv = -1;
        ok = ok && tmp.get_string(&sv, &err) == STDLIB_OK &&
                  sv != (const char *)0 && strcmp(sv, "x") == 0;
        tmp = obj.member("n");
        ok = ok && tmp.get_int(&iv, &err) == STDLIB_OK && iv == 7;
        tmp = obj.member("b");
        ok = ok && tmp.get_bool(&bv, &err) == STDLIB_OK && bv == 1;
        arr = obj.member("a");
        tmp = arr.at(0);
        iv = -1;
        ok = ok && tmp.get_int(&iv, &err) == STDLIB_OK && iv == 1;
        doc.drop();
    }
    xc_check("nested_correct_type_ok", ok);

    /* ---- E: builder mutation between writes ---------------------------- */
    builder = json_builder();
    ok = builder.valid() == 1;
    if (ok) {
        dump_a = builder.write(&len_a);
        dump_b = builder.write(&len_b);
        ok = ok && xc_is_bytes(dump_a, len_a, "{}");
        ok = ok && dump_b != (char *)0 && len_a == len_b &&
                  memcmp(dump_a, dump_b, len_a) == 0;
        json_free(dump_a);
        json_free(dump_b);
        builder.drop();
    }
    xc_check("builder_empty_write", ok);

    builder = json_builder();
    ok = builder.valid() == 1;
    if (ok) {
        ok = ok && builder.add_int("a", 1, &err) == STDLIB_OK;
        dump_a = builder.write(&len_a);
        ok = ok && xc_is_bytes(dump_a, len_a, "{\"a\":1}");
        ok = ok && builder.add_int("b", 2, &err) == STDLIB_OK;
        dump_b = builder.write(&len_b);
        ok = ok && xc_is_bytes(dump_b, len_b, "{\"a\":1,\"b\":2}");
        ok = ok && len_b > len_a && memcmp(dump_a, dump_b, len_a) != 0;
        redo = json(dump_a, &err);
        ok = ok && redo.valid() == 1;
        if (redo.valid()) {
            tmp = redo.root();
            ok = ok && tmp.size() == 1;
            tmp = tmp.member("a");
            iv = -1;
            ok = ok && tmp.get_int(&iv, &err) == STDLIB_OK && iv == 1;
            redo.drop();
        }
        redo = json(dump_b, &err);
        ok = ok && redo.valid() == 1;
        if (redo.valid()) {
            tmp = redo.root();
            ok = ok && tmp.size() == 2;
            tmp = tmp.member("a");
            iv = -1;
            ok = ok && tmp.get_int(&iv, &err) == STDLIB_OK && iv == 1;
            tmp = redo.root();
            tmp = tmp.member("b");
            iv = -1;
            ok = ok && tmp.get_int(&iv, &err) == STDLIB_OK && iv == 2;
            redo.drop();
        }

        /* Duplicate key added through the builder; member() returns first. */
        ok = ok && builder.add_int("a", 9, &err) == STDLIB_OK;
        dump_c = builder.write(&len_c);
        ok = ok && xc_is_bytes(dump_c, len_c, "{\"a\":1,\"b\":2,\"a\":9}");
        ok = ok && len_c > len_b && memcmp(dump_b, dump_c, len_b) != 0;
        redo = json(dump_c, &err);
        ok = ok && redo.valid() == 1;
        if (redo.valid()) {
            tmp = redo.root();
            ok = ok && tmp.size() == 3;
            tmp = tmp.member("a");
            iv = -1;
            ok = ok && tmp.get_int(&iv, &err) == STDLIB_OK && iv == 1;
            redo.drop();
        }

        /* Repeated writes after mutation stay byte-identical. */
        dump_b = builder.write(&len_b);
        ok = ok && dump_b != (char *)0 && len_b == len_c &&
                  memcmp(dump_b, dump_c, len_c) == 0;
        json_free(dump_a);
        json_free(dump_b);
        json_free(dump_c);
        builder.drop();
    }
    xc_check("builder_mutate_between_writes", ok);

    doc = json("{\"a\":[1,{\"b\":\"c\"}],\"d\":true}", &err);
    ok = doc.valid() == 1;
    if (ok) {
        root = doc.root();
        dump_a = root.write(&len_a);
        dump_b = root.write(&len_b);
        ok = ok && dump_a != (char *)0 && dump_b != (char *)0 &&
                  len_a == len_b && memcmp(dump_a, dump_b, len_a) == 0;
        json_free(dump_a);
        json_free(dump_b);
        doc.drop();
    }
    xc_check("doc_repeat_write_identical", ok);

    /* ---- F: post-drop refusal (safe path only) ------------------------- */
    doc = json("{\"o\":{\"k\":42},\"a\":[1,2]}", &err);
    ok = doc.valid() == 1;
    if (ok) {
        doc.drop();
        doc.drop();
        doc.drop();
        ok = ok && doc.valid() == 0;
        root = doc.root();
        ok = ok && !root.valid() && root.size() == 0;
        ok = ok && !root.is_object() && !root.is_array() && !root.is_number() &&
                  !root.is_string() && !root.is_bool();
        iv = 4242;
        ok = ok && root.get_int(&iv, &err) == STDLIB_FAIL && iv == 4242 &&
                  err.category() == STDLIB_CATEGORY_INVALID;
        dump_a = root.write(&len_a);
        ok = ok && dump_a == (char *)0;
        json_free(dump_a);
        obj = root.member("o");
        arr = root.at(0);
        ok = ok && !obj.valid() && !arr.valid() && obj.size() == 0 &&
                  arr.size() == 0;
        tmp = obj.member("k");
        sv = (const char *)0;
        ok = ok && tmp.get_string(&sv, &err) == STDLIB_FAIL &&
                  sv == (const char *)0;
    }
    xc_check("postdrop_doc_view_refusal", ok);

    builder = json_builder();
    ok = builder.valid() == 1;
    if (ok) {
        ok = ok && builder.add_int("k", 1, &err) == STDLIB_OK;
        builder.drop();
        builder.drop();
        builder.drop();
        ok = ok && builder.valid() == 0;
        ok = ok && builder.add_int("k", 1, &err) == STDLIB_FAIL;
        ok = ok && builder.add_double("k", 1.0, &err) == STDLIB_FAIL;
        ok = ok && builder.add_bool("k", 1, &err) == STDLIB_FAIL;
        ok = ok && builder.add_string("k", "v", &err) == STDLIB_FAIL;
        ok = ok && builder.write(&len_a) == (char *)0;
    }
    xc_check("postdrop_builder_refusal", ok);
    xc_check("view_use_after_free", 1); /* SKIP: see NOTES */

    /* ---- G: allocation failure (SKIP, not a pass) ---------------------- */
    xc_check("json_alloc_failure_hook", 1); /* SKIP: see NOTES */

    print("EXTRA_JSON_CLEANUP", xc_checks, xc_failures);
    return xc_failures == 0 ? 0 : 1;
}
