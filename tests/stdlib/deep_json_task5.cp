/* Deep task-5 gap test for stdlib/json.hp.
 *
 * Complements deep_json_types.cp and deep_json_stress.cp (which already cover
 * typed conversions, get_int range/reals, wrong types, NULL out pointers,
 * missing/invalid members, non-finite builder doubles, NULL key/value/path,
 * dropped-builder mutators, repeated writes and bounded stress). This file
 * covers only NEW gaps:
 *   J1  empty / whitespace-only input parse failure with error category,
 *   J2  malformed boundaries: truncated object, unmatched bracket,
 *       trailing comma (object + array), duplicate keys, embedded NUL,
 *   J3  get_double on INT64_MIN/MAX, UINT64_MAX and 1e-308/-1e-308/1e308,
 *   J4  NULL arguments not already covered (NULL text, NULL error pointer on
 *       getters/builders, NULL length on write, json_view_of(NULL),
 *       json_free(NULL)),
 *   J5  invalid state after drop (document views refuse safely, builder
 *       triple-drop idempotent),
 *   J6  allocation-failure hook (SKIP: yyjson allocator is not reachable).
 *
 * Build: c+ ... -Ithird_party/yyjson -Lbuild/ext -lyyjson
 * Self-checking: each check prints "<tag> <0|1>"; the final line is
 * "JSON_TASK5 <checks> <failures>". Exit 0 only when failures == 0.
 */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/json.hp"

static int t5_checks = 0;
static int t5_failures = 0;

static void t5_check(const char *tag, int cond) {
    t5_checks++;
    if (!cond)
        t5_failures++;
    print(tag, cond ? 1 : 0);
}

static void t5_skip(const char *tag, const char *reason) {
    t5_checks++;
    print(tag, 1);
    print("SKIP_REASON", tag, reason);
}

/* Assert a document rejected the text with a well-formed INVALID error. */
static void t5_probe_bad(const char *tag, const char *text) {
    error err = error();
    json doc = json(text, &err);
    int ok;
    ok = doc.valid() == 0;
    ok = ok && err.failed_now() == 1;
    ok = ok && err.category() == STDLIB_CATEGORY_INVALID;
    ok = ok && err.code() == (int)CPLUS_EXIT_UNSUPPORTED;
    ok = ok && strlen(err.what()) > 0;
    t5_check(tag, ok);
    doc.drop();
}

/* Assert get_double on member "n" of a one-member document yields ~expected. */
static void t5_probe_double(const char *tag, const char *text, double expected,
                            double tol) {
    error err = error();
    json doc = json(text, &err);
    int ok = doc.valid() == 1;
    if (ok) {
        json_view root = doc.root();
        json_view v = root.member("n");
        double dv = -1.0;
        int rc = v.get_double(&dv, &err);
        ok = ok && rc == STDLIB_OK && err.failed_now() == 0;
        ok = ok && fabs(dv - expected) <= tol;
        doc.drop();
    }
    t5_check(tag, ok);
}

int main(void) {
    error err = error();

    /* ---- J1: empty and whitespace-only input --------------------------- */
    t5_probe_bad("json_empty_input", "");
    t5_probe_bad("json_whitespace_input", "  \n\t  ");

    /* ---- J2: malformed boundaries -------------------------------------- */
    t5_probe_bad("json_truncated_object", "{");
    t5_probe_bad("json_unmatched_bracket", "[1,2");
    t5_probe_bad("json_trailing_comma_obj", "{\"a\":1,}");
    t5_probe_bad("json_trailing_comma_arr", "[1,]");
    t5_probe_bad("json_bad_escape", "{\"a\":\"\\q\"}");

    /* Duplicate keys: yyjson keeps both entries but member() returns the
     * FIRST value (observed and pinned here). */
    {
        json dup_doc = json("{\"a\":1,\"a\":2}", &err);
        int ok = dup_doc.valid() == 1;
        if (ok) {
            json_view dup_root = dup_doc.root();
            json_view dup_v = dup_root.member("a");
            int iv = -1;
            ok = ok && dup_root.size() == 2;
            ok = ok && dup_v.get_int(&iv, &err) == STDLIB_OK && iv == 1;
            dup_doc.drop();
        }
        t5_check("json_duplicate_keys", ok);
    }

    /* json.hp uses strlen(text) (no explicit-length API). An embedded NUL
     * truncates parsing at that byte: a complete prefix parses, an incomplete
     * prefix is a defined INVALID parse failure. */
    {
        char good_nul[] = {'{','"','a','"',':','1','}', 0,
                           ',','"','b','"',':','2','}', 0};
        char bad_nul[] = {'{','"','a','"',':', 0, '1','}', 0};
        json nul_ok_doc = json(good_nul, &err);
        int ok = nul_ok_doc.valid() == 1;
        if (ok) {
            json_view nul_ok_root = nul_ok_doc.root();
            json_view nul_ok_v = nul_ok_root.member("a");
            int iv = -1;
            ok = ok && nul_ok_root.size() == 1 &&
                 nul_ok_v.get_int(&iv, &err) == STDLIB_OK && iv == 1;
            nul_ok_doc.drop();
        }
        t5_check("json_embedded_nul_complete_prefix", ok);
        t5_probe_bad("json_embedded_nul_incomplete_prefix", bad_nul);
    }

    /* ---- J3: boundary numbers via get_double --------------------------- */
    /* get_int range refusal for these same values is already covered by
     * deep_json_types.cp T1; only the double view is new here. */
    t5_probe_double("json_double_i64max", "{\"n\":9223372036854775807}",
                    9223372036854775808.0, 1.0e12);
    t5_probe_double("json_double_i64min", "{\"n\":-9223372036854775808}",
                    -9223372036854775808.0, 1.0e12);
    t5_probe_double("json_double_u64max", "{\"n\":18446744073709551615}",
                    18446744073709551616.0, 2.0e12);
    t5_probe_double("json_double_denorm_min", "{\"n\":1e-308}", 1e-308, 1e-309);
    t5_probe_double("json_double_denorm_neg", "{\"n\":-1e-308}", -1e-308, 1e-309);
    t5_probe_double("json_double_1e308", "{\"n\":1e308}", 1e308, 1e295);

    /* ---- J4: NULL arguments not covered by deep_json_types.cp ---------- */
    {
        json nulltext_doc = json((const char *)0, &err);
        /* The constructor deliberately resets err and records no failure for
         * a NULL source (the file helper fills it in). */
        t5_check("json_null_text",
                 nulltext_doc.valid() == 0 && err.failed_now() == 0 &&
                     err.category() == STDLIB_CATEGORY_OK &&
                     err.code() == (int)CPLUS_EXIT_OK);
    }
    {
        json mid_doc = json("{\"n\":7,\"s\":\"x\",\"b\":true}", &err);
        int ok = mid_doc.valid() == 1;
        if (ok) {
            json_view mid_root = mid_doc.root();
            json_view mid_n = mid_root.member("n");
            json_view mid_s = mid_root.member("s");
            json_view mid_b = mid_root.member("b");
            int iv = 0;
            int bv = 0;
            double dv = 0.0;
            const char *sv = (const char *)0;
            char *dump;
            ok = ok && mid_n.get_int(&iv, (error *)0) == STDLIB_OK && iv == 7;
            ok = ok && mid_n.get_double(&dv, (error *)0) == STDLIB_OK &&
                       fabs(dv - 7.0) < 1e-12;
            ok = ok && mid_b.get_bool(&bv, (error *)0) == STDLIB_OK && bv == 1;
            ok = ok && mid_s.get_string(&sv, (error *)0) == STDLIB_OK &&
                       sv != (const char *)0 && strcmp(sv, "x") == 0;
            /* Wrong type with a NULL error pointer must refuse, not crash. */
            ok = ok && mid_b.get_int(&iv, (error *)0) == STDLIB_FAIL;
            /* write(NULL length) is accepted by yyjson. */
            dump = mid_root.write((size_t *)0);
            ok = ok && dump != (char *)0;
            json_free(dump);
            json_free((char *)0); /* no-op */
            mid_doc.drop();
        }
        t5_check("json_null_err_and_write_args", ok);
    }
    {
        json_view nil_view = json_view_of((const yyjson_val *)0);
        int iv = 4242;
        int rc = nil_view.get_int(&iv, &err);
        t5_check("json_view_of_null",
                 nil_view.valid() == 0 && nil_view.size() == 0 &&
                     nil_view.is_object() == 0 && nil_view.is_array() == 0 &&
                     nil_view.is_number() == 0 && nil_view.is_string() == 0 &&
                     nil_view.is_bool() == 0 && rc == STDLIB_FAIL &&
                     iv == 4242 && err.category() == STDLIB_CATEGORY_INVALID);
    }
    {
        json_builder bld = json_builder();
        int ok = bld.valid() == 1;
        char *dump;
        if (ok) {
            ok = ok && bld.add_int("i", 5, (error *)0) == STDLIB_OK;
            ok = ok && bld.add_double("d", 0.5, (error *)0) == STDLIB_OK;
            ok = ok && bld.add_bool("b", 1, (error *)0) == STDLIB_OK;
            ok = ok && bld.add_string("s", "v", (error *)0) == STDLIB_OK;
            dump = bld.write((size_t *)0);
            ok = ok && dump != (char *)0;
            json_free(dump);
            bld.drop();
        }
        t5_check("json_builder_null_err_args", ok);
    }

    /* ---- J5: invalid state after drop ---------------------------------- */
    {
        json dropped_doc = json("{\"k\":42}", &err);
        int ok = dropped_doc.valid() == 1;
        json_view root_after_drop;
        json_view child_after_drop;
        json_view member_after_drop;
        int iv = 4242;
        char *dump;
        if (ok) {
            dropped_doc.drop();
            dropped_doc.drop(); /* idempotent */
            dropped_doc.drop(); /* triple */
            ok = ok && dropped_doc.valid() == 0;
        }
        root_after_drop = dropped_doc.root();
        child_after_drop = root_after_drop.at(0);
        member_after_drop = root_after_drop.member("k");
        ok = ok && root_after_drop.valid() == 0;
        ok = ok && root_after_drop.size() == 0;
        ok = ok && root_after_drop.is_object() == 0 &&
                   root_after_drop.is_array() == 0 &&
                   root_after_drop.is_number() == 0 &&
                   root_after_drop.is_string() == 0 &&
                   root_after_drop.is_bool() == 0;
        ok = ok && !child_after_drop.valid();
        ok = ok && !member_after_drop.valid();
        ok = ok && root_after_drop.get_int(&iv, &err) == STDLIB_FAIL &&
                   iv == 4242 && err.category() == STDLIB_CATEGORY_INVALID;
        dump = root_after_drop.write((size_t *)0);
        ok = ok && dump == (char *)0;
        t5_check("json_dropped_doc_views", ok);
    }
    {
        json_builder dropped_bld = json_builder();
        int ok = dropped_bld.valid() == 1;
        dropped_bld.drop();
        dropped_bld.drop();
        dropped_bld.drop();
        ok = ok && dropped_bld.valid() == 0;
        ok = ok && dropped_bld.add_int("k", 1, (error *)0) == STDLIB_FAIL;
        ok = ok && dropped_bld.add_double("k", 1.0, (error *)0) == STDLIB_FAIL;
        ok = ok && dropped_bld.add_bool("k", 1, (error *)0) == STDLIB_FAIL;
        ok = ok && dropped_bld.add_string("k", "v", (error *)0) == STDLIB_FAIL;
        ok = ok && dropped_bld.write((size_t *)0) == (char *)0;
        t5_check("json_dropped_builder_triple", ok);
    }

    /* ---- J6: allocation-failure hook (SKIP) ---------------------------- */
    t5_skip("json_alloc_failure_hook",
            "json.hp passes a NULL yyjson_alc to yyjson_read_opts() and "
            "yyjson_mut_doc_new(); yyjson's allocator is per-call with no "
            "global hook and json.hp exposes no allocator setter. "
            "stdlib_alloc_fail_at/after therefore cannot reach yyjson "
            "allocations, so no honest OOM test can be fabricated here "
            "(see DEEP_JSON_CONTRACT.md section 3).");

    print("JSON_TASK5", t5_checks, t5_failures);
    return t5_failures == 0 ? 0 : 1;
}
