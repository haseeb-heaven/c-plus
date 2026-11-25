/* Task 3 Phase 4 — JSON robustness: numeric edges.
 *
 * Covers: invalid number tokens, integer overflow boundaries (get_int must
 * refuse out-of-range values rather than wrap), and floating edge cases
 * (64-bit extremes, subnormals, max/min finite doubles, -0.0). A number that
 * yyjson cannot represent must be a controlled parse failure, never a crash.
 *
 * Build (separate argv operands):
 *   c+ build this.cp --strict --sanitize=none \
 *       -Ithird_party/yyjson -Lbuild/ext -lyyjson -o OUT
 * Self-checking: each check prints "<tag> <0|1>"; final line is
 * "JSON_NUMERIC_EDGES <checks> <failures>". Exit 0 only when failures == 0.
 *
 * Strict-mode compliance: no pointer-parameter subscripts, no variable
 * modulo/division, unique variable names.
 */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/json.hp"

static int jne_checks = 0;
static int jne_failures = 0;

static void jne_check(const char *tag, int cond) {
    jne_checks++;
    if (!cond)
        jne_failures++;
    print(tag, cond ? 1 : 0);
}

/* Assert a JSON doc with member "n" cannot be parsed (controlled failure). */
static void jne_bad_number(const char *tag, const char *number_text) {
    char jne_buf[128];
    error jne_err = error();
    json jne_doc;
    int jne_ok;
    snprintf(jne_buf, sizeof(jne_buf), "{\"n\":%s}", number_text);
    jne_doc = json(jne_buf, &jne_err);
    jne_ok = jne_doc.valid() == 0;
    jne_ok = jne_ok && jne_err.failed_now() == 1;
    jne_ok = jne_ok && jne_err.category() == STDLIB_CATEGORY_INVALID;
    jne_check(tag, jne_ok);
    jne_doc.drop();
}

/* Parse {"n":<text>}, read member "n" as int, return get_int's rc.
 * Returns -99 if the document itself failed to parse. */
static int jne_probe_int(const char *number_text, int *out, error *err) {
    char jne_buf[128];
    json jne_doc;
    json_view jne_root;
    json_view jne_v;
    int jne_rc;
    snprintf(jne_buf, sizeof(jne_buf), "{\"n\":%s}", number_text);
    jne_doc = json(jne_buf, err);
    if (!jne_doc.valid())
        return -99;
    jne_root = jne_doc.root();
    jne_v = jne_root.member("n");
    jne_rc = jne_v.get_int(out, err);
    jne_doc.drop();
    return jne_rc;
}

/* Parse {"n":<text>}, read member "n" as double. Returns -99 on parse fail. */
static int jne_probe_double(const char *number_text, double *out, error *err) {
    char jne_buf[128];
    json jne_doc;
    json_view jne_root;
    json_view jne_v;
    int jne_rc;
    snprintf(jne_buf, sizeof(jne_buf), "{\"n\":%s}", number_text);
    jne_doc = json(jne_buf, err);
    if (!jne_doc.valid())
        return -99;
    jne_root = jne_doc.root();
    jne_v = jne_root.member("n");
    jne_rc = jne_v.get_double(out, err);
    jne_doc.drop();
    return jne_rc;
}

int main(void) {
    error jne_err = error();
    int jne_iv;
    double jne_dv;
    int jne_rc;

    /* ---- Invalid number tokens ---------------------------------------- */
    jne_bad_number("jne_inf_overflow", "1e400");
    jne_bad_number("jne_neg_inf_overflow", "-1e400");
    jne_bad_number("jne_nan_token", "NaN");
    jne_bad_number("jne_inf_token", "Infinity");
    jne_bad_number("jne_neg_inf_token", "-Infinity");
    jne_bad_number("jne_hex_number", "0x10");
    jne_bad_number("jne_leading_plus", "+1");
    jne_bad_number("jne_double_dot", "1.2.3");
    jne_bad_number("jne_exp_empty", "1e");
    jne_bad_number("jne_underscore", "1_000");

    /* ---- Integer overflow boundaries: get_int refuses, never wraps ----- */
    jne_iv = -1;
    jne_rc = jne_probe_int("2147483647", &jne_iv, &jne_err);
    jne_check("jne_int_max_ok",
              jne_rc == STDLIB_OK && jne_iv == 2147483647 &&
                  jne_err.failed_now() == 0);

    jne_iv = -1;
    jne_rc = jne_probe_int("-2147483648", &jne_iv, &jne_err);
    jne_check("jne_int_min_ok",
              jne_rc == STDLIB_OK && jne_iv == (-2147483647 - 1));

    jne_iv = 4242;
    jne_rc = jne_probe_int("2147483648", &jne_iv, &jne_err);
    jne_check("jne_int_max_plus_one",
              jne_rc == STDLIB_FAIL && jne_iv == 4242 &&
                  jne_err.category() == STDLIB_CATEGORY_RANGE &&
                  jne_err.code() == (int)CPLUS_EXIT_BOUNDS);

    jne_iv = 4242;
    jne_rc = jne_probe_int("-2147483649", &jne_iv, &jne_err);
    jne_check("jne_int_min_minus_one",
              jne_rc == STDLIB_FAIL && jne_iv == 4242 &&
                  jne_err.category() == STDLIB_CATEGORY_RANGE);

    jne_iv = 4242;
    jne_rc = jne_probe_int("9223372036854775807", &jne_iv, &jne_err);
    jne_check("jne_i64_max_refused",
              jne_rc == STDLIB_FAIL && jne_iv == 4242 &&
                  jne_err.category() == STDLIB_CATEGORY_RANGE);

    jne_iv = 4242;
    jne_rc = jne_probe_int("-9223372036854775808", &jne_iv, &jne_err);
    jne_check("jne_i64_min_refused",
              jne_rc == STDLIB_FAIL && jne_iv == 4242 &&
                  jne_err.category() == STDLIB_CATEGORY_RANGE);

    jne_iv = 4242;
    jne_rc = jne_probe_int("18446744073709551615", &jne_iv, &jne_err);
    jne_check("jne_u64_max_refused",
              jne_rc == STDLIB_FAIL && jne_iv == 4242 &&
                  jne_err.category() == STDLIB_CATEGORY_RANGE);

    /* Beyond uint64: stored as a real and still refused by get_int. */
    jne_iv = 4242;
    jne_rc = jne_probe_int("18446744073709551616", &jne_iv, &jne_err);
    jne_check("jne_beyond_u64_refused",
              jne_rc == STDLIB_FAIL && jne_iv == 4242 &&
                  jne_err.category() == STDLIB_CATEGORY_INVALID);

    /* A very long integer literal parses as a double (not a parse error) but
     * must still be refused by get_int without wrapping or crashing. */
    jne_iv = 4242;
    jne_rc = jne_probe_int("9999999999999999999999999999999999999999", &jne_iv,
                           &jne_err);
    jne_check("jne_huge_int_refused",
              jne_rc == STDLIB_FAIL && jne_iv == 4242 &&
                  jne_err.category() == STDLIB_CATEGORY_INVALID);

    /* An integral real is not an integer: no 1.0 -> 1 silent conversion. */
    jne_iv = 4242;
    jne_rc = jne_probe_int("1.0", &jne_iv, &jne_err);
    jne_check("jne_real_not_int",
              jne_rc == STDLIB_FAIL && jne_iv == 4242 &&
                  jne_err.category() == STDLIB_CATEGORY_INVALID);

    /* ---- Floating edge cases via get_double --------------------------- */
    jne_dv = -1.0;
    jne_rc = jne_probe_double("9223372036854775807", &jne_dv, &jne_err);
    jne_check("jne_dbl_i64max",
              jne_rc == STDLIB_OK && fabs(jne_dv - 9223372036854775808.0) < 1.0e12);

    jne_dv = -1.0;
    jne_rc = jne_probe_double("-9223372036854775808", &jne_dv, &jne_err);
    jne_check("jne_dbl_i64min",
              jne_rc == STDLIB_OK && fabs(jne_dv - (-9223372036854775808.0)) < 1.0e12);

    jne_dv = -1.0;
    jne_rc = jne_probe_double("18446744073709551615", &jne_dv, &jne_err);
    jne_check("jne_dbl_u64max",
              jne_rc == STDLIB_OK && fabs(jne_dv - 18446744073709551616.0) < 2.0e12);

    jne_dv = -1.0;
    jne_rc = jne_probe_double("1e-308", &jne_dv, &jne_err);
    jne_check("jne_dbl_subnormal_1e308",
              jne_rc == STDLIB_OK && jne_dv > 0.0 && fabs(jne_dv - 1e-308) < 1e-309);

    jne_dv = -1.0;
    jne_rc = jne_probe_double("5e-324", &jne_dv, &jne_err);
    jne_check("jne_dbl_min_subnormal",
              jne_rc == STDLIB_OK && jne_dv > 0.0 && jne_dv < 1e-320);

    jne_dv = -1.0;
    jne_rc = jne_probe_double("1.7976931348623157e308", &jne_dv, &jne_err);
    jne_check("jne_dbl_max_finite",
              jne_rc == STDLIB_OK && isfinite(jne_dv) &&
                  fabs(jne_dv - 1.7976931348623157e308) < 1.0e295);

    jne_dv = -1.0;
    jne_rc = jne_probe_double("1e308", &jne_dv, &jne_err);
    jne_check("jne_dbl_1e308",
              jne_rc == STDLIB_OK && isfinite(jne_dv) &&
                  fabs(jne_dv - 1e308) < 1.0e295);

    jne_dv = -1.0;
    jne_rc = jne_probe_double("-0.0", &jne_dv, &jne_err);
    jne_check("jne_dbl_neg_zero",
              jne_rc == STDLIB_OK && jne_dv == 0.0 && fabs(jne_dv) < 1e-300);

    print("JSON_NUMERIC_EDGES", jne_checks, jne_failures);
    return jne_failures == 0 ? 0 : 1;
}
