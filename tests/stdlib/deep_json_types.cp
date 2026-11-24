/* Deep contract test for stdlib/json.hp -- typed conversions and API guards.
 *
 * Scope (see docs/stdlib/DEEP_JSON_CONTRACT.md):
 *   T1 integer range and fractional conversion (no silent truncation, no
 *      acceptance of out-of-range or non-integral numbers);
 *   T2 wrong types for every typed getter;
 *   T3 NULL output/key/path arguments;
 *   T4 missing members, invalid array indices, non-array at();
 *   T5 builder failures (invalid builder, non-finite double, NULL strings)
 *      and recoverable error categories/codes;
 *   T6 repeated serialization and borrowed-view lifetime rules.
 *
 * Self-checking: every check prints "<label> <1|0>" and the final line is
 * "RESULT <checks> <failed>". Exit 0 means every check passed; exit 1 means
 * at least one contract was violated. No deliberate fatal is executed here:
 * the library's compile-time copy rejection and the view-outlives-owner rule
 * are recorded as one-shot observations in the contract document.
 */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/json.hp"

static int g_checks = 0;
static int g_failed = 0;

static void check(const char *label, int cond) {
    g_checks++;
    if (!cond)
        g_failed++;
    print(label, cond ? 1 : 0);
}

/* Parse one-object document, read member "n" with get_int, drop, return rc.
 * A separate sentinel (-99) reports a parse failure. */
static int probe_int(const char *js, int *out, error *err) {
    json doc;
    json_view root;
    json_view v;
    int rc;
    doc = json(js, err);
    if (!doc.valid())
        return -99;
    root = doc.root();
    v = root.member("n");
    rc = v.get_int(out, err);
    doc.drop();
    return rc;
}

/* Same probe for get_double. */
static int probe_double(const char *js, double *out, error *err) {
    json doc;
    json_view root;
    json_view v;
    int rc;
    doc = json(js, err);
    if (!doc.valid())
        return -99;
    root = doc.root();
    v = root.member("n");
    rc = v.get_double(out, err);
    doc.drop();
    return rc;
}

int main(void) {
    error err = error();
    int iv = 0;
    double dv = 0;
    const char *sv = 0; /* out-param: initialise to satisfy strict lifetime analysis */
    int bv = 0;
    int rc = 0;
    size_t len_a = 0;   /* out-param: initialise to satisfy strict lifetime analysis */
    size_t len_b = 0;
    char *out_a = 0;
    char *out_b = 0;
    json doc;
    json_view root;
    json_view v;
    json_view missing;
    json_view arr;
    json_view bad_view;
    json_builder builder;

    /* ---- T1: integer range, no silent truncation ------------------------ */
    iv = -1;
    rc = probe_int("{\"n\":0}", &iv, &err);
    check("t_int_zero", rc == STDLIB_OK && iv == 0 && err.failed_now() == 0);

    iv = -1;
    rc = probe_int("{\"n\":2147483647}", &iv, &err);
    check("t_int_max", rc == STDLIB_OK && iv == 2147483647 &&
                           err.failed_now() == 0);

    iv = -1;
    rc = probe_int("{\"n\":-2147483648}", &iv, &err);
    check("t_int_min", rc == STDLIB_OK && iv == (-2147483647 - 1) &&
                           err.failed_now() == 0);

    iv = -1;
    rc = probe_int("{\"n\":-1}", &iv, &err);
    check("t_int_negative", rc == STDLIB_OK && iv == -1);

    /* INT_MAX+1 must be rejected, not wrapped to INT_MIN. */
    iv = 4242;
    rc = probe_int("{\"n\":2147483648}", &iv, &err);
    check("t_int_over_max", rc == STDLIB_FAIL && iv == 4242 &&
                                err.failed_now() == 1 &&
                                err.category() == STDLIB_CATEGORY_RANGE &&
                                err.code() == CPLUS_EXIT_BOUNDS);

    /* INT_MIN-1 must be rejected, not wrapped to INT_MAX. */
    iv = 4242;
    rc = probe_int("{\"n\":-2147483649}", &iv, &err);
    check("t_int_under_min", rc == STDLIB_FAIL && iv == 4242 &&
                                 err.category() == STDLIB_CATEGORY_RANGE &&
                                 err.code() == CPLUS_EXIT_BOUNDS);

    /* 64-bit extremes and an out-of-int64 decimal: reject, never truncate. */
    iv = 4242;
    rc = probe_int("{\"n\":9223372036854775807}", &iv, &err);
    check("t_int_i64max", rc == STDLIB_FAIL && iv == 4242 &&
                              err.category() == STDLIB_CATEGORY_RANGE);

    iv = 4242;
    rc = probe_int("{\"n\":-9223372036854775808}", &iv, &err);
    check("t_int_i64min", rc == STDLIB_FAIL && iv == 4242 &&
                              err.category() == STDLIB_CATEGORY_RANGE);

    iv = 4242;
    rc = probe_int("{\"n\":18446744073709551615}", &iv, &err);
    check("t_int_u64max", rc == STDLIB_FAIL && iv == 4242 &&
                              err.category() == STDLIB_CATEGORY_RANGE);

    /* Beyond uint64: yyjson stores it as a real; get_int must still refuse. */
    iv = 4242;
    rc = probe_int("{\"n\":99999999999999999999999999999999}", &iv, &err);
    check("t_int_huge_real", rc == STDLIB_FAIL && iv == 4242 &&
                                 err.category() == STDLIB_CATEGORY_INVALID);

    /* ---- T1b: fractional conversion ------------------------------------ */
    dv = -1;
    rc = probe_double("{\"n\":1.5}", &dv, &err);
    check("t_dbl_real", rc == STDLIB_OK && fabs(dv - 1.5) < 1e-12 &&
                            err.failed_now() == 0);

    dv = -1;
    rc = probe_double("{\"n\":2147483648}", &dv, &err);
    check("t_dbl_big_int", rc == STDLIB_OK && dv == 2147483648.0);

    dv = -1;
    rc = probe_double("{\"n\":1e-9}", &dv, &err);
    check("t_dbl_small", rc == STDLIB_OK && fabs(dv - 1e-9) < 1e-20);

    /* An integral real is still not an int: no 1.0 -> 1 silent conversion. */
    iv = 4242;
    rc = probe_int("{\"n\":1.0}", &iv, &err);
    check("t_real_not_int", rc == STDLIB_FAIL && iv == 4242 &&
                                err.category() == STDLIB_CATEGORY_INVALID &&
                                err.code() == CPLUS_EXIT_UNSUPPORTED);

    iv = 4242;
    rc = probe_int("{\"n\":1e2}", &iv, &err);
    check("t_exp_not_int", rc == STDLIB_FAIL && iv == 4242 &&
                               err.category() == STDLIB_CATEGORY_INVALID);
    iv = 4242;
    rc = probe_int("{\"n\":-0.0}", &iv, &err);
    check("t_negzero_not_int", rc == STDLIB_FAIL && iv == 4242 &&
                                   err.category() == STDLIB_CATEGORY_INVALID);

    /* A parse overflow is a recoverable parse failure, not infinity. */
    doc = json("{\"n\":1e400}", &err);
    check("t_dbl_inf_parse_fail", doc.valid() == 0 && err.failed_now() == 1 &&
                                      err.category() == STDLIB_CATEGORY_INVALID);
    doc.drop();

    /* ---- T2/T3/T4: type errors, NULL args, missing members/indices ----- */
    doc = json("{\"s\":\"hi\",\"n\":5,\"b\":true,\"arr\":[1,2,3],"
               "\"obj\":{\"k\":7},\"z\":null}", &err);
    check("t_doc_ok", doc.valid() == 1 && err.failed_now() == 0);
    root = doc.root();

    v = root.member("s");
    iv = 4242;
    rc = v.get_int(&iv, &err);
    check("t_getint_on_str", rc == STDLIB_FAIL && iv == 4242 &&
                                 err.category() == STDLIB_CATEGORY_INVALID);
    v = root.member("b");
    rc = v.get_double(&dv, &err);
    check("t_getdbl_on_bool", rc == STDLIB_FAIL &&
                                  err.category() == STDLIB_CATEGORY_INVALID);
    v = root.member("n");
    rc = v.get_string(&sv, &err);
    check("t_getstr_on_num", rc == STDLIB_FAIL &&
                                 err.category() == STDLIB_CATEGORY_INVALID);
    v = root.member("arr");
    rc = v.get_bool(&bv, &err);
    check("t_getbool_on_arr", rc == STDLIB_FAIL &&
                                  err.category() == STDLIB_CATEGORY_INVALID);
    v = root.member("obj");
    rc = v.get_bool(&bv, &err);
    check("t_getbool_on_obj", rc == STDLIB_FAIL &&
                                  err.category() == STDLIB_CATEGORY_INVALID);

    /* JSON null is a real yyjson value: a valid view, but no typed getter. */
    v = root.member("z");
    iv = 4242;
    rc = v.get_int(&iv, &err);
    check("t_json_null_view", v.valid() == 1 && v.is_object() == 0 &&
                                  v.is_array() == 0 && v.is_number() == 0 &&
                                  v.is_string() == 0 && v.is_bool() == 0 &&
                                  v.size() == 0 &&
                                  rc == STDLIB_FAIL && iv == 4242 &&
                                  err.category() == STDLIB_CATEGORY_INVALID);

    /* NULL output pointers: all four getters refuse with INVALID. */
    v = root.member("n");
    rc = v.get_int((int *)0, &err);
    check("t_null_out_int", rc == STDLIB_FAIL &&
                                err.category() == STDLIB_CATEGORY_INVALID);
    rc = v.get_double((double *)0, &err);
    check("t_null_out_dbl", rc == STDLIB_FAIL &&
                                err.category() == STDLIB_CATEGORY_INVALID);
    rc = v.get_bool((int *)0, &err);
    check("t_null_out_bool", rc == STDLIB_FAIL &&
                                 err.category() == STDLIB_CATEGORY_INVALID);
    v = root.member("s");
    rc = v.get_string((const char **)0, &err);
    check("t_null_out_str", rc == STDLIB_FAIL &&
                                err.category() == STDLIB_CATEGORY_INVALID);

    /* NULL key / missing key both produce an invalid, safe-to-use view. */
    bad_view = root.member((const char *)0);
    check("t_null_key", bad_view.valid() == 0 && bad_view.size() == 0);
    missing = root.member("absent");
    iv = 4242;
    rc = missing.get_int(&iv, &err);
    check("t_missing_member", missing.valid() == 0 && rc == STDLIB_FAIL &&
                                  iv == 4242 &&
                                  err.category() == STDLIB_CATEGORY_INVALID);

    /* Array bounds, size_t overflow, and at() on non-array views. */
    arr = root.member("arr");
    check("t_arr_size", arr.is_array() == 1 && arr.size() == 3);
    v = arr.at(0);
    rc = v.get_int(&iv, &err);
    check("t_arr_first", v.valid() == 1 && rc == STDLIB_OK && iv == 1);
    v = arr.at(2);
    rc = v.get_int(&iv, &err);
    check("t_arr_last", v.valid() == 1 && rc == STDLIB_OK && iv == 3);
    v = arr.at(3);
    check("t_arr_oob", v.valid() == 0);
    v = arr.at((size_t)-1);
    check("t_arr_oob_max", v.valid() == 0);
    /* at() on a scalar/object/null view is invalid, never a wild pointer. */
    v = root.member("s");
    bad_view = v.at(0);
    check("t_at_on_str", bad_view.valid() == 0);
    v = root.member("obj");
    bad_view = v.at(0);
    check("t_at_on_obj", bad_view.valid() == 0);
    v = root.member("n");
    bad_view = v.at(0);
    check("t_at_on_num", bad_view.valid() == 0);
    v = root.member("z");
    bad_view = v.at(0);
    check("t_at_on_null", bad_view.valid() == 0);
    bad_view = arr.member("k");
    check("t_member_on_arr", bad_view.valid() == 0);
    doc.drop();

    /* ---- T5: builder failure paths ------------------------------------- */
    builder = json_builder();
    check("t_builder_ok", builder.valid() == 1);
    rc = builder.add_int("i", 42, &err);
    check("t_add_int", rc == STDLIB_OK && err.failed_now() == 0);
    rc = builder.add_bool("b", 1, &err);
    check("t_add_bool", rc == STDLIB_OK);
    rc = builder.add_double("d", 0.25, &err);
    check("t_add_double", rc == STDLIB_OK);
    rc = builder.add_string("s", "a\"b\\c", &err);
    check("t_add_string", rc == STDLIB_OK);
    out_a = builder.write(&len_a);
    out_b = builder.write(&len_b);
    check("t_builder_repeat_write",
          out_a != 0 && out_b != 0 && len_a == len_b &&
              memcmp(out_a, out_b, len_a) == 0);
    check("t_builder_escape",
          out_a != 0 && strstr(out_a, "\\\"") != (char *)0 &&
              strstr(out_a, "\\\\") != (char *)0);
    json_free(out_a);
    json_free(out_b);

    /* Non-finite doubles are rejected as a RANGE error, not written as
     * non-standard JSON tokens. */
    rc = builder.add_double("nan", NAN, &err);
    check("t_add_nan", rc == STDLIB_FAIL &&
                           err.category() == STDLIB_CATEGORY_RANGE &&
                           err.code() == CPLUS_EXIT_BOUNDS);
    rc = builder.add_double("inf", INFINITY, &err);
    check("t_add_inf", rc == STDLIB_FAIL &&
                           err.category() == STDLIB_CATEGORY_RANGE);
    rc = builder.add_string((const char *)0, "v", &err);
    check("t_add_null_key", rc == STDLIB_FAIL &&
                                err.category() == STDLIB_CATEGORY_INVALID);
    rc = builder.add_string("k", (const char *)0, &err);
    check("t_add_null_value", rc == STDLIB_FAIL &&
                                  err.category() == STDLIB_CATEGORY_INVALID);

    /* After drop() the builder is invalid; every mutator must refuse
     * recoverably instead of writing through a freed handle. */
    builder.drop();
    check("t_builder_dropped", builder.valid() == 0);
    rc = builder.add_int("i", 1, &err);
    check("t_dropped_add_int", rc == STDLIB_FAIL &&
                                   err.category() == STDLIB_CATEGORY_INVALID);
    rc = builder.add_double("d", 1.0, &err);
    check("t_dropped_add_dbl", rc == STDLIB_FAIL &&
                                   err.category() == STDLIB_CATEGORY_INVALID);
    rc = builder.add_bool("b", 1, &err);
    check("t_dropped_add_bool", rc == STDLIB_FAIL &&
                                    err.category() == STDLIB_CATEGORY_INVALID);
    rc = builder.add_string("s", "v", &err);
    check("t_dropped_add_str", rc == STDLIB_FAIL &&
                                   err.category() == STDLIB_CATEGORY_INVALID);
    check("t_dropped_write", builder.write(&len_a) == 0);
    builder.drop(); /* idempotent */
    check("t_builder_double_drop", builder.valid() == 0);

    /* ---- T6: repeated serialization + borrowed view lifetime ----------- */
    doc = json("{\"a\":[1,2,{\"b\":\"c\"}],\"d\":true}", &err);
    root = doc.root();
    out_a = root.write(&len_a);
    out_b = root.write(&len_b);
    check("t_doc_repeat_write",
          out_a != 0 && out_b != 0 && len_a == len_b &&
              memcmp(out_a, out_b, len_a) == 0);
    /* Re-serializing the dump must yield the same canonical text. */
    doc.drop();
    json_free(out_b);

    doc = json(out_a, &err);
    root = doc.root();
    out_b = root.write(&len_b);
    check("t_roundtrip_write",
          out_b != 0 && len_b == len_a &&
              memcmp(out_b, out_a, len_a) == 0);
    json_free(out_b);
    json_free(out_a);

    v = root.member("a");
    arr = v.at(2);
    v = arr.member("b");
    rc = v.get_string(&sv, &err);
    check("t_nested_borrow", rc == STDLIB_OK && sv != 0 &&
                                 memcmp(sv, "c", 2) == 0);
    /* Views are borrowed: they must be consumed before the owner is dropped. */
    doc.drop();
    check("t_owner_dropped", doc.valid() == 0);

    doc = json("{}", &err);
    doc.drop();
    root = doc.root();
    check("t_invalid_doc_root", root.valid() == 0);
    out_a = root.write(&len_a);
    check("t_invalid_doc_write", out_a == 0);

    /* NULL path is a recoverable failure. json.hp deliberately labels the
     * bad argument INVALID while keeping the IO exit code; real file/parse
     * failures report CATEGORY_IO / CATEGORY_INVALID respectively. */
    doc = json_parse_file((const char *)0, &err);
    check("t_null_path", doc.valid() == 0 && err.failed_now() == 1 &&
                             err.category() == STDLIB_CATEGORY_INVALID &&
                             err.code() == CPLUS_EXIT_IO);
    doc.drop();

    print("RESULT", g_checks, g_failed);
    return g_failed == 0 ? 0 : 1;
}
