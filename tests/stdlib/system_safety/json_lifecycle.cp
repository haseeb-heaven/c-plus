/* Task 3 Phase 4 — JSON robustness: types, missing fields, cycles, OOM SKIP.
 *
 * Covers: missing fields, wrong-type getter refusals, and repeated
 * parse/write/drop cycles (documents and builders). Allocation-failure
 * injection is recorded as SKIP: yyjson's allocator is not injectable through
 * stdlib/json.hp, so no honest OOM test can be fabricated here.
 *
 * Build (separate argv operands):
 *   c+ build this.cp --strict --sanitize=none \
 *       -Ithird_party/yyjson -Lbuild/ext -lyyjson -o OUT
 * Self-checking: each check prints "<tag> <0|1>"; final line is
 * "JSON_LIFECYCLE <checks> <failures>". Exit 0 only when failures == 0.
 * The "1" printed for jlc_alloc_failure_hook is a SKIP marker (see NOTES),
 * not a pass claim.
 *
 * Strict-mode compliance: no pointer-parameter subscripts, no variable
 * modulo/division (the valid/malformed toggle uses a boolean flag), unique
 * variable names.
 */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/json.hp"

static int jlc_checks = 0;
static int jlc_failures = 0;

static void jlc_check(const char *tag, int cond) {
    jlc_checks++;
    if (!cond)
        jlc_failures++;
    print(tag, cond ? 1 : 0);
}

static void jlc_skip(const char *tag, const char *reason) {
    jlc_checks++;
    print(tag, 1); /* SKIP marker, not a pass claim */
    print("SKIP_REASON", tag, reason);
}

int main(void) {
    error jlc_err = error();
    json jlc_doc;
    json_view jlc_root;
    json_view jlc_v;
    json_view jlc_missing;
    json_view jlc_null_view;
    int jlc_iv;
    double jlc_dv;
    const char *jlc_sv;
    int jlc_bv;
    int jlc_rc;
    size_t jlc_len_a;
    size_t jlc_len_b;
    size_t jlc_len_c;
    char *jlc_out_a;
    char *jlc_out_b;
    char *jlc_out_c;
    int jlc_i;
    int jlc_valid_count = 0;
    int jlc_fail_count = 0;
    int jlc_is_valid = 1;
    int jlc_bad = 0;

    /* ---- Missing fields ------------------------------------------------ */
    jlc_doc = json("{\"s\":\"hi\",\"n\":5,\"b\":true,\"arr\":[1,2,3],"
                   "\"obj\":{\"k\":7},\"z\":null}", &jlc_err);
    jlc_check("jlc_doc_ok", jlc_doc.valid() == 1 && jlc_err.failed_now() == 0);
    jlc_root = jlc_doc.root();

    jlc_missing = jlc_root.member("absent");
    jlc_iv = 4242;
    jlc_rc = jlc_missing.get_int(&jlc_iv, &jlc_err);
    jlc_check("jlc_missing_field",
              jlc_missing.valid() == 0 && jlc_missing.size() == 0 &&
                  jlc_rc == STDLIB_FAIL && jlc_iv == 4242 &&
                  jlc_err.category() == STDLIB_CATEGORY_INVALID);

    jlc_v = jlc_root.member("obj");
    jlc_missing = jlc_v.member("absent");
    jlc_check("jlc_missing_nested_field", jlc_missing.valid() == 0);

    jlc_missing = jlc_root.member((const char *)0);
    jlc_check("jlc_null_key", jlc_missing.valid() == 0);

    /* ---- Wrong types --------------------------------------------------- */
    jlc_v = jlc_root.member("s");
    jlc_iv = 4242;
    jlc_rc = jlc_v.get_int(&jlc_iv, &jlc_err);
    jlc_check("jlc_int_on_string",
              jlc_rc == STDLIB_FAIL && jlc_iv == 4242 &&
                  jlc_err.category() == STDLIB_CATEGORY_INVALID &&
                  jlc_v.is_string() == 1 && jlc_v.is_number() == 0);

    jlc_v = jlc_root.member("b");
    jlc_dv = -1.0;
    jlc_rc = jlc_v.get_double(&jlc_dv, &jlc_err);
    jlc_check("jlc_double_on_bool",
              jlc_rc == STDLIB_FAIL && jlc_dv == -1.0 &&
                  jlc_err.category() == STDLIB_CATEGORY_INVALID &&
                  jlc_v.is_bool() == 1);

    jlc_v = jlc_root.member("n");
    jlc_sv = (const char *)0;
    jlc_rc = jlc_v.get_string(&jlc_sv, &jlc_err);
    jlc_check("jlc_string_on_number",
              jlc_rc == STDLIB_FAIL && jlc_sv == (const char *)0 &&
                  jlc_err.category() == STDLIB_CATEGORY_INVALID &&
                  jlc_v.is_number() == 1);

    jlc_v = jlc_root.member("arr");
    jlc_bv = -1;
    jlc_rc = jlc_v.get_bool(&jlc_bv, &jlc_err);
    jlc_check("jlc_bool_on_array",
              jlc_rc == STDLIB_FAIL && jlc_bv == -1 &&
                  jlc_err.category() == STDLIB_CATEGORY_INVALID &&
                  jlc_v.is_array() == 1);

    jlc_v = jlc_root.member("obj");
    jlc_rc = jlc_v.get_bool(&jlc_bv, &jlc_err);
    jlc_check("jlc_bool_on_object",
              jlc_rc == STDLIB_FAIL && jlc_err.category() == STDLIB_CATEGORY_INVALID &&
                  jlc_v.is_object() == 1);

    /* JSON null is a valid view with no typed getter. */
    jlc_null_view = jlc_root.member("z");
    jlc_iv = 4242;
    jlc_rc = jlc_null_view.get_int(&jlc_iv, &jlc_err);
    jlc_check("jlc_json_null_view",
              jlc_null_view.valid() == 1 && jlc_null_view.size() == 0 &&
                  jlc_null_view.is_object() == 0 && jlc_null_view.is_array() == 0 &&
                  jlc_null_view.is_number() == 0 && jlc_null_view.is_string() == 0 &&
                  jlc_null_view.is_bool() == 0 && jlc_rc == STDLIB_FAIL &&
                  jlc_iv == 4242);

    /* at()/member() on the wrong kind of view are invalid, never wild. */
    jlc_missing = jlc_root.at(0);
    jlc_check("jlc_at_on_object", jlc_missing.valid() == 0);
    jlc_v = jlc_root.member("arr");
    jlc_missing = jlc_v.member("k");
    jlc_check("jlc_member_on_array", jlc_missing.valid() == 0);
    jlc_missing = jlc_v.at((size_t)-1);
    jlc_check("jlc_at_oob", jlc_missing.valid() == 0);
    jlc_doc.drop();

    /* ---- Repeated parse/write/drop cycles ------------------------------ */
    for (jlc_i = 0; jlc_i < 200; jlc_i++) {
        if (jlc_is_valid) {
            json jlc_cyc = json("{\"a\":[1,2,{\"b\":\"c\"}],\"d\":true,"
                                "\"n\":42}", &jlc_err);
            if (!jlc_cyc.valid()) {
                jlc_bad = 1;
            } else {
                json_view jlc_croot = jlc_cyc.root();
                jlc_out_a = jlc_croot.write(&jlc_len_a);
                jlc_out_b = jlc_croot.write(&jlc_len_b);
                if (jlc_out_a == (char *)0 || jlc_out_b == (char *)0 ||
                    jlc_len_a != jlc_len_b ||
                    memcmp(jlc_out_a, jlc_out_b, jlc_len_a) != 0) {
                    jlc_bad = 1;
                } else {
                    json jlc_cyc2 = json(jlc_out_a, &jlc_err);
                    if (!jlc_cyc2.valid()) {
                        jlc_bad = 1;
                    } else {
                        json_view jlc_croot2 = jlc_cyc2.root();
                        jlc_out_c = jlc_croot2.write(&jlc_len_c);
                        if (jlc_out_c == (char *)0 || jlc_len_c != jlc_len_a ||
                            memcmp(jlc_out_c, jlc_out_a, jlc_len_a) != 0)
                            jlc_bad = 1;
                        json_free(jlc_out_c);
                        jlc_cyc2.drop();
                    }
                }
                json_free(jlc_out_b);
                json_free(jlc_out_a);
                jlc_cyc.drop();
                jlc_valid_count++;
            }
            jlc_is_valid = 0;
        } else {
            json jlc_baddoc = json("{\"a\": [1,2,", &jlc_err);
            if (jlc_baddoc.valid() ||
                jlc_err.category() != STDLIB_CATEGORY_INVALID)
                jlc_bad = 1;
            jlc_baddoc.drop();
            jlc_fail_count++;
            jlc_is_valid = 1;
        }
    }
    jlc_check("jlc_cycles_200_no_crash", jlc_bad == 0);
    jlc_check("jlc_valid_count_100", jlc_valid_count == 100);
    jlc_check("jlc_fail_count_100", jlc_fail_count == 100);

    /* ---- Builder cycles ------------------------------------------------ */
    jlc_bad = 0;
    for (jlc_i = 0; jlc_i < 50; jlc_i++) {
        json_builder jlc_bld = json_builder();
        if (!jlc_bld.valid()) {
            jlc_bad = 1;
        } else {
            char *jlc_bout_a;
            char *jlc_bout_b;
            if (jlc_bld.add_int("i", jlc_i, &jlc_err) != STDLIB_OK ||
                jlc_bld.add_string("s", "value", &jlc_err) != STDLIB_OK ||
                jlc_bld.add_bool("b", 1, &jlc_err) != STDLIB_OK)
                jlc_bad = 1;
            jlc_bout_a = jlc_bld.write(&jlc_len_a);
            jlc_bout_b = jlc_bld.write(&jlc_len_b);
            if (jlc_bout_a == (char *)0 || jlc_bout_b == (char *)0 ||
                jlc_len_a != jlc_len_b ||
                memcmp(jlc_bout_a, jlc_bout_b, jlc_len_a) != 0)
                jlc_bad = 1;
            json_free(jlc_bout_b);
            json_free(jlc_bout_a);
            jlc_bld.drop();
            jlc_bld.drop(); /* idempotent */
        }
    }
    jlc_check("jlc_builder_cycles_50", jlc_bad == 0);

    /* ---- Allocation failures: SKIP ------------------------------------ */
    jlc_skip("jlc_alloc_failure_hook",
             "json.hp passes a NULL yyjson_alc to every read/write/mutate "
             "call; yyjson's allocator is per-call with no global injection "
             "point and json.hp exposes no allocator setter. "
             "stdlib_alloc_fail_at/after cannot reach yyjson allocations, so "
             "no honest OOM test can be fabricated (see json_NOTES.md).");

    print("JSON_LIFECYCLE", jlc_checks, jlc_failures);
    return jlc_failures == 0 ? 0 : 1;
}
