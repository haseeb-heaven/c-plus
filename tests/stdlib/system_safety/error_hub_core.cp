/* Task 3: stdlib/error.hp as the error propagation hub - CORE semantics.
 *
 * Self-checking, deterministic, single translation unit. Every check prints
 * "<tag> <0|1>" and the last line is "ERROR_HUB_CORE <checks> <failures>".
 * Exit code is 0 only when failures == 0. No stdin, no clock, no randomness,
 * no external library and no fatal probe is used; all loops are bounded and
 * all inputs are fixed literals (fixed seed = fully deterministic).
 *
 * Coverage:
 *   A  construct / clear / reset / NULL free-helper calls, explicit category;
 *   B  exact message truncation boundaries (0,1,254,255,256,257,300) and NULL,
 *      including the long -> short no-stale-tail property;
 *   C  code -> category mapping for all cplus_exit_code values plus the
 *      "unknown" category-name fallback and explicit category override;
 *   D  callback reentrancy and the per-thread recursion guard (single TU):
 *      one callback per outer failure, no overflow, no stuck latch, NULL
 *      message delivered as "", and the exact nested-failure counter deltas;
 *   E  telemetry on/off parity: identical error records with telemetry disabled
 *      and enabled, zero counters when off, exact counters when on;
 *   F  repeated failure/recovery cycles (128 fixed-pattern iterations) with
 *      no stale message fields and a clean final reset.
 *
 * Ownership: this file, error_hub_system.cp and error_NOTES.md only. No header
 * was edited to force a pass.
 */
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"

static int ehc_checks = 0;
static int ehc_failures = 0;

static void ehc_check(const char *tag, int cond) {
    ehc_checks++;
    if (!cond)
        ehc_failures++;
    print(tag, cond ? 1 : 0);
}

/* ---- D: reentrant callback state --------------------------------------- */
static error *ehc_cb_target = (error *)0;
static int ehc_cb_calls = 0;
static int ehc_cb_depth = 0;
static int ehc_cb_overflow = 0;
static int ehc_cb_last_cat = -999;
static int ehc_cb_last_code = -999;
static int ehc_cb_msg_len = -999;
static int ehc_cb_msg_null = 0;

static void ehc_reentrant_cb(int category, cplus_exit_code code,
                             const char *message) {
    ehc_cb_calls++;
    if (ehc_cb_depth != 0) {
        ehc_cb_overflow++;
        return;
    }
    ehc_cb_depth++;
    ehc_cb_last_cat = category;
    ehc_cb_last_code = (int)code;
    if (message == (const char *)0) {
        ehc_cb_msg_null = 1;
        ehc_cb_msg_len = -1;
    } else {
        ehc_cb_msg_null = 0;
        ehc_cb_msg_len = (int)strlen(message);
    }
    if (ehc_cb_target != (error *)0)
        error_fail(ehc_cb_target, CPLUS_EXIT_RESOURCE, "nested");
    ehc_cb_depth--;
}

/* Record the observable state of an error object for telemetry parity. */
struct ehc_sig {
    int failed;
    int category;
    int code;
    int len;
    char text[STDLIB_ERROR_SIZE];
};

static void ehc_capture(struct ehc_sig *out, error *slot) {
    int n;
    out->failed = slot->failed_now();
    out->category = slot->category();
    out->code = slot->code();
    n = (int)strlen(slot->what());
    out->len = n;
    if (n > (int)STDLIB_ERROR_SIZE - 1)
        n = (int)STDLIB_ERROR_SIZE - 1;
    __builtin_memset(out->text, 0, sizeof(out->text));
    __builtin_memcpy(out->text, slot->what(), (size_t)n);
}

/* A fixed, allocation-free failure sequence used by section E. */
static void ehc_failure_sequence(error *slot) {
    error_fail(slot, CPLUS_EXIT_IO, "seq-io");
    error_fail(slot, CPLUS_EXIT_BOUNDS, "seq-bounds");
    error_fail(slot, CPLUS_EXIT_RESOURCE, "seq-memory");
    error_fail(slot, CPLUS_EXIT_OWNERSHIP, "seq-ownership");
    error_fail(slot, CPLUS_EXIT_DOUBLE_FREE, "seq-lifecycle");
    error_fail_cat(slot, 777, (cplus_exit_code)4242, "seq-unknown");
}

int main(void) {
    stdlib_telemetry_stats ehc_tm;
    stdlib_telemetry_stats ehc_tm_off;
    struct ehc_sig ehc_on[6];
    struct ehc_sig ehc_off[6];
    int ehc_i;

    /* Start from a known process-wide state. */
    stdlib_set_error_callback((stdlib_error_callback)0);
    stdlib_alloc_fail_reset();
    stdlib_telemetry_reset();
    stdlib_telemetry_set_enabled(0);

    /* ---- A: construct / clear / reset / NULL helpers ---------------------- */
    {
        error ehc_a_slot = error();
        error ehc_a_guard = error();
        ehc_check("ehc_a_fresh",
                  ehc_a_slot.ok() == 1 && ehc_a_slot.failed_now() == 0 &&
                      ehc_a_slot.code() == (int)CPLUS_EXIT_OK &&
                      ehc_a_slot.category() == STDLIB_CATEGORY_OK &&
                      strcmp(ehc_a_slot.what(), "") == 0);

        ehc_a_slot.set(CPLUS_EXIT_IO, "a-message");
        ehc_check("ehc_a_set",
                  ehc_a_slot.failed_now() == 1 &&
                      ehc_a_slot.code() == (int)CPLUS_EXIT_IO &&
                      ehc_a_slot.category() == STDLIB_CATEGORY_IO &&
                      strcmp(ehc_a_slot.what(), "a-message") == 0);

        ehc_a_slot.clear();
        ehc_check("ehc_a_clear",
                  ehc_a_slot.ok() == 1 && ehc_a_slot.code() == 0 &&
                      ehc_a_slot.category() == STDLIB_CATEGORY_OK &&
                      strcmp(ehc_a_slot.what(), "") == 0);

        error_fail(&ehc_a_slot, CPLUS_EXIT_RESOURCE, "b-message");
        error_reset(&ehc_a_slot);
        ehc_check("ehc_a_reset",
                  ehc_a_slot.ok() == 1 && ehc_a_slot.failed_now() == 0 &&
                      ehc_a_slot.code() == 0 &&
                      ehc_a_slot.category() == STDLIB_CATEGORY_OK &&
                      strcmp(ehc_a_slot.what(), "") == 0);

        /* NULL targets are no-ops, not crashes; the guard value is untouched. */
        ehc_a_guard.set(CPLUS_EXIT_BOUNDS, "guarded");
        error_reset((void *)0);
        error_fail((void *)0, CPLUS_EXIT_IO, "null-target");
        error_fail_cat((void *)0, STDLIB_CATEGORY_RANGE, CPLUS_EXIT_BOUNDS,
                       "null-target-cat");
        ehc_check("ehc_a_null_helpers",
                  ehc_a_guard.failed_now() == 1 &&
                      ehc_a_guard.code() == (int)CPLUS_EXIT_BOUNDS &&
                      strcmp(ehc_a_guard.what(), "guarded") == 0);

        ehc_a_slot.set_category(73, CPLUS_EXIT_UNSUPPORTED, "explicit");
        ehc_check("ehc_a_explicit_category",
                  ehc_a_slot.failed_now() == 1 && ehc_a_slot.category() == 73 &&
                      ehc_a_slot.code() == (int)CPLUS_EXIT_UNSUPPORTED &&
                      strcmp(ehc_a_slot.what(), "explicit") == 0);

        error_fail_cat(&ehc_a_slot, STDLIB_CATEGORY_RANGE, CPLUS_EXIT_BOUNDS,
                       "range");
        ehc_check("ehc_a_category_override",
                  ehc_a_slot.category() == STDLIB_CATEGORY_RANGE &&
                      ehc_a_slot.code() == (int)CPLUS_EXIT_BOUNDS &&
                      strcmp(ehc_a_slot.what(), "range") == 0);

        error_reset(&ehc_a_slot);
        ehc_a_slot.set(CPLUS_EXIT_IO, "after-reset");
        ehc_check("ehc_a_reuse_after_clear",
                  ehc_a_slot.failed_now() == 1 &&
                      ehc_a_slot.code() == (int)CPLUS_EXIT_IO &&
                      strcmp(ehc_a_slot.what(), "after-reset") == 0);
    }

    /* ---- B: exact truncation boundaries, including NULL ------------------ */
    {
        static const int ehc_lengths[7] = {0, 1, 254, 255, 256, 257, 300};
        static const char *ehc_tags[7] = {
            "ehc_b_len_0", "ehc_b_len_1", "ehc_b_len_254", "ehc_b_len_255",
            "ehc_b_len_256", "ehc_b_len_257", "ehc_b_len_300"};
        error ehc_b_slot = error();
        char ehc_payload[320];
        int ehc_j;
        int ehc_k;
        for (ehc_k = 0; ehc_k < 7; ehc_k++) {
            int n = ehc_lengths[ehc_k];
            int want = n < (int)STDLIB_ERROR_SIZE - 1
                           ? n
                           : (int)STDLIB_ERROR_SIZE - 1;
            int ok;
            for (ehc_j = 0; ehc_j < n; ehc_j++)
                ehc_payload[ehc_j] = 'Z';
            ehc_payload[n] = '\0';
            error_fail_cat(&ehc_b_slot, STDLIB_CATEGORY_RANGE,
                           CPLUS_EXIT_BOUNDS, ehc_payload);
            ok = ehc_b_slot.failed_now() == 1 &&
                 ehc_b_slot.category() == STDLIB_CATEGORY_RANGE &&
                 ehc_b_slot.code() == (int)CPLUS_EXIT_BOUNDS &&
                 (int)strlen(ehc_b_slot.what()) == want &&
                 ehc_b_slot.what()[(int)STDLIB_ERROR_SIZE - 1] == '\0' &&
                 memcmp(ehc_b_slot.what(), ehc_payload, (size_t)want) == 0;
            ehc_check(ehc_tags[ehc_k], ok);
        }

        error_fail_cat(&ehc_b_slot, STDLIB_CATEGORY_IO, CPLUS_EXIT_IO,
                       (const char *)0);
        ehc_check("ehc_b_null_message",
                  ehc_b_slot.failed_now() == 1 &&
                      ehc_b_slot.category() == STDLIB_CATEGORY_IO &&
                      ehc_b_slot.code() == (int)CPLUS_EXIT_IO &&
                      strcmp(ehc_b_slot.what(), "") == 0 &&
                      ehc_b_slot.what()[(int)STDLIB_ERROR_SIZE - 1] == '\0');

        error_fail_cat(&ehc_b_slot, STDLIB_CATEGORY_INVALID,
                       CPLUS_EXIT_UNSUPPORTED, "");
        ehc_check("ehc_b_empty_message",
                  strcmp(ehc_b_slot.what(), "") == 0 &&
                      ehc_b_slot.category() == STDLIB_CATEGORY_INVALID &&
                      ehc_b_slot.code() == (int)CPLUS_EXIT_UNSUPPORTED);

        /* Long message then short message: no stale tail past the new NUL. */
        for (ehc_j = 0; ehc_j < 300; ehc_j++)
            ehc_payload[ehc_j] = 'Q';
        ehc_payload[300] = '\0';
        error_fail_cat(&ehc_b_slot, STDLIB_CATEGORY_MEMORY,
                       CPLUS_EXIT_RESOURCE, ehc_payload);
        error_fail_cat(&ehc_b_slot, STDLIB_CATEGORY_INVALID,
                       CPLUS_EXIT_UNSUPPORTED, "s");
        ehc_check("ehc_b_no_stale_tail",
                  strcmp(ehc_b_slot.what(), "s") == 0 &&
                      ehc_b_slot.what()[1] == '\0' &&
                      ehc_b_slot.category() == STDLIB_CATEGORY_INVALID &&
                      ehc_b_slot.code() == (int)CPLUS_EXIT_UNSUPPORTED);
    }

    /* ---- C: code -> category mapping ------------------------------------- */
    {
        error ehc_c_slot = error();
        error_fail(&ehc_c_slot, CPLUS_EXIT_OK, "m");
        ehc_check("ehc_c_code_ok",
                  ehc_c_slot.failed_now() == 1 &&
                      ehc_c_slot.category() == STDLIB_CATEGORY_INVALID &&
                      ehc_c_slot.code() == (int)CPLUS_EXIT_OK);
        error_fail(&ehc_c_slot, CPLUS_EXIT_IO, "m");
        ehc_check("ehc_c_code_io",
                  ehc_c_slot.category() == STDLIB_CATEGORY_IO &&
                      ehc_c_slot.code() == (int)CPLUS_EXIT_IO);
        error_fail(&ehc_c_slot, CPLUS_EXIT_RESOURCE, "m");
        ehc_check("ehc_c_code_resource",
                  ehc_c_slot.category() == STDLIB_CATEGORY_MEMORY &&
                      ehc_c_slot.code() == (int)CPLUS_EXIT_RESOURCE);
        error_fail(&ehc_c_slot, CPLUS_EXIT_BOUNDS, "m");
        ehc_check("ehc_c_code_bounds",
                  ehc_c_slot.category() == STDLIB_CATEGORY_BOUNDS &&
                      ehc_c_slot.code() == (int)CPLUS_EXIT_BOUNDS);
        error_fail(&ehc_c_slot, CPLUS_EXIT_OWNERSHIP, "m");
        ehc_check("ehc_c_code_ownership",
                  ehc_c_slot.category() == STDLIB_CATEGORY_OWNERSHIP &&
                      ehc_c_slot.code() == (int)CPLUS_EXIT_OWNERSHIP);
        error_fail(&ehc_c_slot, CPLUS_EXIT_USE_AFTER_FREE, "m");
        ehc_check("ehc_c_code_uaf",
                  ehc_c_slot.category() == STDLIB_CATEGORY_LIFECYCLE &&
                      ehc_c_slot.code() == (int)CPLUS_EXIT_USE_AFTER_FREE);
        error_fail(&ehc_c_slot, CPLUS_EXIT_DOUBLE_FREE, "m");
        ehc_check("ehc_c_code_double_free",
                  ehc_c_slot.category() == STDLIB_CATEGORY_LIFECYCLE &&
                      ehc_c_slot.code() == (int)CPLUS_EXIT_DOUBLE_FREE);
        error_fail(&ehc_c_slot, CPLUS_EXIT_UNSUPPORTED, "m");
        ehc_check("ehc_c_code_unsupported",
                  ehc_c_slot.category() == STDLIB_CATEGORY_INVALID &&
                      ehc_c_slot.code() == (int)CPLUS_EXIT_UNSUPPORTED);
        error_fail(&ehc_c_slot, CPLUS_EXIT_NULL_POINTER, "m");
        ehc_check("ehc_c_code_nullptr",
                  ehc_c_slot.category() == STDLIB_CATEGORY_INVALID &&
                      ehc_c_slot.code() == (int)CPLUS_EXIT_NULL_POINTER);
        error_fail(&ehc_c_slot, CPLUS_EXIT_TYPE_CONFUSION, "m");
        ehc_check("ehc_c_code_type_confusion",
                  ehc_c_slot.category() == STDLIB_CATEGORY_INVALID &&
                      ehc_c_slot.code() == (int)CPLUS_EXIT_TYPE_CONFUSION);
        error_fail(&ehc_c_slot, (cplus_exit_code)7777, "m");
        ehc_check("ehc_c_code_unknown",
                  ehc_c_slot.category() == STDLIB_CATEGORY_INVALID &&
                      ehc_c_slot.code() == 7777);

        ehc_check("ehc_c_names",
                  strcmp(stdlib_category_name(STDLIB_CATEGORY_OK), "ok") == 0 &&
                  strcmp(stdlib_category_name(STDLIB_CATEGORY_INVALID),
                         "invalid") == 0 &&
                  strcmp(stdlib_category_name(STDLIB_CATEGORY_RANGE), "range") ==
                      0 &&
                  strcmp(stdlib_category_name(STDLIB_CATEGORY_IO), "io") == 0 &&
                  strcmp(stdlib_category_name(STDLIB_CATEGORY_MEMORY),
                         "memory") == 0 &&
                  strcmp(stdlib_category_name(STDLIB_CATEGORY_BOUNDS),
                         "bounds") == 0 &&
                  strcmp(stdlib_category_name(STDLIB_CATEGORY_OWNERSHIP),
                         "ownership") == 0 &&
                  strcmp(stdlib_category_name(STDLIB_CATEGORY_LIFECYCLE),
                         "lifecycle") == 0 &&
                  strcmp(stdlib_category_name(STDLIB_CATEGORY_UTF8), "utf8") ==
                      0 &&
                  strcmp(stdlib_category_name(999), "unknown") == 0 &&
                  strcmp(stdlib_category_name(-5), "unknown") == 0);
    }

    /* ---- D: reentrant callback + recursion guard (single TU) ------------- */
    {
        error ehc_d_slot = error();
        ehc_cb_target = &ehc_d_slot;
        ehc_cb_calls = 0;
        ehc_cb_depth = 0;
        ehc_cb_overflow = 0;
        ehc_cb_msg_null = 0;
        ehc_cb_msg_len = -999;
        stdlib_telemetry_reset();
        stdlib_telemetry_set_enabled(1);
        stdlib_set_error_callback(ehc_reentrant_cb);

        error_fail(&ehc_d_slot, CPLUS_EXIT_IO, "outer");
        ehc_check("ehc_d_no_recursion",
                  ehc_cb_calls == 1 && ehc_cb_overflow == 0 &&
                      ehc_cb_depth == 0 && ehc_cb_last_cat == STDLIB_CATEGORY_IO &&
                      ehc_cb_last_code == (int)CPLUS_EXIT_IO &&
                      ehc_cb_msg_len == 5 && ehc_cb_msg_null == 0);
        /* Outer fields are written after stdlib_note() returns, so the nested
         * failure is counted but must not overwrite the outer record. */
        ehc_check("ehc_d_outer_wins",
                  ehc_d_slot.failed_now() == 1 &&
                      ehc_d_slot.category() == STDLIB_CATEGORY_IO &&
                      ehc_d_slot.code() == (int)CPLUS_EXIT_IO &&
                      strcmp(ehc_d_slot.what(), "outer") == 0);

        ehc_cb_calls = 0;
        error_fail(&ehc_d_slot, CPLUS_EXIT_BOUNDS, "second");
        ehc_check("ehc_d_no_latch",
                  ehc_cb_calls == 1 && ehc_cb_overflow == 0 &&
                      ehc_cb_depth == 0 &&
                      strcmp(ehc_d_slot.what(), "second") == 0 &&
                      ehc_d_slot.category() == STDLIB_CATEGORY_BOUNDS);

        ehc_cb_calls = 0;
        ehc_cb_msg_len = -999;
        ehc_cb_msg_null = 0;
        error_fail_cat(&ehc_d_slot, STDLIB_CATEGORY_LIFECYCLE,
                       CPLUS_EXIT_IO, (const char *)0);
        /* stdlib_note() normalises a NULL message to "" before calling back,
         * so the callback must see a non-NULL zero-length message. */
        ehc_check("ehc_d_null_msg_to_callback",
                  ehc_cb_calls == 1 && ehc_cb_msg_null == 0 &&
                      ehc_cb_msg_len == 0 &&
                      ehc_cb_last_cat == STDLIB_CATEGORY_LIFECYCLE &&
                      strcmp(ehc_d_slot.what(), "") == 0);

        /* 3 outer notes (IO, BOUNDS, LIFECYCLE) + 3 nested notes (MEMORY),
         * each also incrementing api_failures. */
        stdlib_telemetry_snapshot(&ehc_tm);
        ehc_check("ehc_d_nested_counters",
                  ehc_tm.api_failures == 6ULL &&
                      ehc_tm.invalid_ops == 1ULL &&
                      ehc_tm.bounds_violations == 1ULL &&
                      ehc_tm.lifecycle_failures == 1ULL &&
                      ehc_tm.alloc_failures == 3ULL);

        stdlib_set_error_callback((stdlib_error_callback)0);
        ehc_cb_calls = 0;
        error_fail(&ehc_d_slot, CPLUS_EXIT_IO, "no-callback");
        ehc_check("ehc_d_callback_cleared",
                  ehc_cb_calls == 0 && ehc_d_slot.failed_now() == 1 &&
                      strcmp(ehc_d_slot.what(), "no-callback") == 0);
        ehc_cb_target = (error *)0;
        stdlib_telemetry_set_enabled(0);
    }

    /* ---- E: telemetry on/off parity -------------------------------------- */
    {
        error ehc_e_slot = error();
        int ehc_same = 1;
        stdlib_set_error_callback((stdlib_error_callback)0);

        stdlib_telemetry_reset();
        stdlib_telemetry_set_enabled(1);
        ehc_check("ehc_e_enabled_flag", stdlib_telemetry_is_enabled() == 1);
        ehc_failure_sequence(&ehc_e_slot);
        for (ehc_i = 0; ehc_i < 6; ehc_i++)
            ehc_capture(&ehc_on[ehc_i], &ehc_e_slot);
        stdlib_telemetry_snapshot(&ehc_tm);

        stdlib_telemetry_reset();
        stdlib_telemetry_set_enabled(0);
        ehc_check("ehc_e_disabled_flag", stdlib_telemetry_is_enabled() == 0);
        ehc_failure_sequence(&ehc_e_slot);
        for (ehc_i = 0; ehc_i < 6; ehc_i++)
            ehc_capture(&ehc_off[ehc_i], &ehc_e_slot);
        stdlib_telemetry_snapshot(&ehc_tm_off);

        for (ehc_i = 0; ehc_i < 6; ehc_i++) {
            int k;
            if (ehc_on[ehc_i].failed != ehc_off[ehc_i].failed ||
                ehc_on[ehc_i].category != ehc_off[ehc_i].category ||
                ehc_on[ehc_i].code != ehc_off[ehc_i].code ||
                ehc_on[ehc_i].len != ehc_off[ehc_i].len)
                ehc_same = 0;
            for (k = 0; k < (int)STDLIB_ERROR_SIZE; k++) {
                if (ehc_on[ehc_i].text[k] != ehc_off[ehc_i].text[k])
                    ehc_same = 0;
            }
        }
        ehc_check("ehc_e_record_parity", ehc_same == 1);
        ehc_check("ehc_e_off_zero",
                  ehc_tm_off.alloc_count == 0ULL &&
                      ehc_tm_off.free_count == 0ULL &&
                      ehc_tm_off.bytes_allocated == 0ULL &&
                      ehc_tm_off.alloc_failures == 0ULL &&
                      ehc_tm_off.invalid_ops == 0ULL &&
                      ehc_tm_off.bounds_violations == 0ULL &&
                      ehc_tm_off.ownership_violations == 0ULL &&
                      ehc_tm_off.lifecycle_failures == 0ULL &&
                      ehc_tm_off.container_growths == 0ULL &&
                      ehc_tm_off.api_failures == 0ULL);
        ehc_check("ehc_e_on_counters",
                  ehc_tm.api_failures == 6ULL &&
                      ehc_tm.invalid_ops == 2ULL && /* IO + unknown code */
                      ehc_tm.bounds_violations == 1ULL &&
                      ehc_tm.alloc_failures == 1ULL &&
                      ehc_tm.ownership_violations == 1ULL &&
                      ehc_tm.lifecycle_failures == 1ULL &&
                      ehc_tm.alloc_count == 0ULL && ehc_tm.free_count == 0ULL);

        /* Re-enable: counters move again from the reset baseline. */
        stdlib_telemetry_set_enabled(1);
        error_fail(&ehc_e_slot, CPLUS_EXIT_IO, "re-enabled");
        stdlib_telemetry_snapshot(&ehc_tm);
        ehc_check("ehc_e_reenable",
                  ehc_tm.api_failures == 1ULL && ehc_tm.invalid_ops == 1ULL);
        stdlib_telemetry_set_enabled(0);
        stdlib_telemetry_snapshot(&ehc_tm_off);
        error_fail(&ehc_e_slot, CPLUS_EXIT_IO, "off-again");
        stdlib_telemetry_snapshot(&ehc_tm_off);
        ehc_check("ehc_e_off_again",
                  ehc_tm_off.api_failures == 1ULL &&
                      strcmp(ehc_e_slot.what(), "off-again") == 0);
        stdlib_telemetry_snapshot((stdlib_telemetry_stats *)0);
    }

    /* ---- F: bounded failure/recovery cycles, no stale state -------------- */
    {
        error ehc_f_slot = error();
        char ehc_f_payload[300];
        int ehc_round;
        int ehc_j;
        int ehc_ok = 1;
        for (ehc_j = 0; ehc_j < 300; ehc_j++)
            ehc_f_payload[ehc_j] = 'R';
        for (ehc_round = 0; ehc_round < 128; ehc_round++) {
            if ((ehc_round & 1) == 0) {
                ehc_f_payload[259] = '\0'; /* 259 chars -> 255 stored */
                error_fail_cat(&ehc_f_slot, STDLIB_CATEGORY_BOUNDS,
                               CPLUS_EXIT_BOUNDS, ehc_f_payload);
                if (ehc_f_slot.failed_now() != 1 ||
                    ehc_f_slot.category() != STDLIB_CATEGORY_BOUNDS ||
                    ehc_f_slot.code() != (int)CPLUS_EXIT_BOUNDS ||
                    (int)strlen(ehc_f_slot.what()) !=
                        (int)STDLIB_ERROR_SIZE - 1)
                    ehc_ok = 0;
            } else {
                error_reset(&ehc_f_slot);
                if (ehc_f_slot.ok() != 1 ||
                    strcmp(ehc_f_slot.what(), "") != 0)
                    ehc_ok = 0;
                error_fail(&ehc_f_slot, CPLUS_EXIT_IO, "s");
                if (ehc_f_slot.failed_now() != 1 ||
                    ehc_f_slot.category() != STDLIB_CATEGORY_IO ||
                    strcmp(ehc_f_slot.what(), "s") != 0 ||
                    ehc_f_slot.what()[1] != '\0')
                    ehc_ok = 0;
            }
            if ((ehc_round % 32) == 31) {
                ehc_f_slot.clear();
                if (ehc_f_slot.ok() != 1 ||
                    ehc_f_slot.category() != STDLIB_CATEGORY_OK ||
                    ehc_f_slot.code() != 0 ||
                    strcmp(ehc_f_slot.what(), "") != 0)
                    ehc_ok = 0;
            }
        }
        ehc_check("ehc_f_128_cycles", ehc_ok == 1);
        error_reset(&ehc_f_slot);
        ehc_check("ehc_f_final_clean",
                  ehc_f_slot.ok() == 1 && ehc_f_slot.failed_now() == 0 &&
                      ehc_f_slot.category() == STDLIB_CATEGORY_OK &&
                      ehc_f_slot.code() == (int)CPLUS_EXIT_OK &&
                      strcmp(ehc_f_slot.what(), "") == 0);
    }

    print("ERROR_HUB_CORE", ehc_checks, ehc_failures);
    return ehc_failures == 0 ? 0 : 1;
}
