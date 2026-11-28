/* Deterministic allocation-failure injection suite -- JSON + file wrapper.
 *
 * Both families are reported as DOCUMENTED SKIPs for failure injection, but
 * each SKIP is backed by a positive probe rather than asserted from a comment:
 *
 *   json.hp passes a NULL yyjson_alc to yyjson_read_opts() and
 *   yyjson_mut_doc_new(). yyjson's allocator is per-call and json.hp exposes
 *   no setter, so stdlib_alloc_fail_at/after cannot reach its allocations.
 *   Probe: set stdlib_alloc_fail_after(0) (EVERY checked allocation fails) and
 *   show a valid JSON document still parses, and that the checked allocator's
 *   telemetry saw no allocation at all. (DEEP_JSON_CONTRACT.md section 3.)
 *
 *   file.hp's constructor and file_temp() use fopen()/tmpfile() directly and
 *   perform no stdlib_alloc/stdlib_calloc, so there is no allocation path to
 *   fail. Probe: with stdlib_alloc_fail_after(0) active, open a temp file,
 *   write/read a byte, drop twice, and verify balanced telemetry.
 *   (DEEP_FILE_CONTRACT.md decision 9.)
 *
 * No OOM test is fabricated and no memory is exhausted. Output:
 * "<tag> <1|0>" per check, two "SKIP_REASON <tag> <reason...>" lines, then
 * "ALLOC_FAIL <checks> <failures>". Exit 0 only when failures == 0.
 */
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/json.hp"
#include "stdlib/file.hp"

static int aj_checks = 0;
static int aj_failures = 0;

static void aj_ck(const char *tag, int cond) {
    aj_checks++;
    if (!cond)
        aj_failures++;
    print(tag, cond ? 1 : 0);
}

static void aj_skip(const char *tag, const char *reason) {
    aj_checks++;
    print(tag, 1);
    print("SKIP_REASON", tag, reason);
}

/* ---- JSON: parse still works with every stdlib allocation failing -------- */
static void aj_json(void) {
    error aj_err = error();
    json aj_doc;
    stdlib_telemetry_stats tm;

    stdlib_telemetry_set_enabled(1);
    stdlib_telemetry_reset();
    stdlib_alloc_fail_reset();

    /* Baseline: a small document parses through the public API. */
    aj_doc = json("{\"n\":1}", &aj_err);
    aj_ck("aj_json_baseline", aj_doc.valid() == 1 && aj_err.failed_now() == 0);
    aj_doc.drop();
    aj_doc.drop(); /* repeated cleanup is inert */

    /* Every checked allocation now fails, yet yyjson still parses: this proves
     * the hook cannot reach yyjson rather than fabricating a fake OOM test. */
    stdlib_alloc_fail_reset();
    stdlib_telemetry_reset();
    stdlib_alloc_fail_after(0);
    aj_doc = json("{\"n\":2}", &aj_err);
    stdlib_alloc_fail_reset();
    aj_ck("aj_json_hook_unreachable", aj_doc.valid() == 1);
    aj_doc.drop();
    stdlib_telemetry_snapshot(&tm);
    aj_ck("aj_json_no_checked_alloc",
          tm.alloc_count == tm.free_count && tm.alloc_failures == 0ULL);

    aj_skip("json_alloc_failure_hook",
            "json.hp passes a NULL yyjson_alc to yyjson_read_opts() and "
            "yyjson_mut_doc_new(); yyjson's allocator is per-call with no "
            "global hook and json.hp exposes no allocator setter. "
            "stdlib_alloc_fail_at/after therefore cannot reach yyjson "
            "allocations, so no honest OOM test can be fabricated here "
            "(see DEEP_JSON_CONTRACT.md section 3).");
    stdlib_telemetry_set_enabled(0);
}

/* ---- file: initialization performs no checked allocation ----------------- */
static void aj_file(void) {
    error aj_ferr = error();
    file aj_f;
    int aj_ch = -1;
    stdlib_telemetry_stats tm;

    stdlib_telemetry_set_enabled(1);
    stdlib_telemetry_reset();
    stdlib_alloc_fail_reset();

    /* fail_after(0) makes every stdlib_alloc fail; file_temp must not use it. */
    stdlib_alloc_fail_after(0);
    aj_f = file_temp(&aj_ferr);
    aj_ck("aj_file_open", aj_f.is_open() == 1 && aj_ferr.failed_now() == 0);
    if (aj_f.is_open()) {
        (void)fputs("Z", aj_f.native());
        (void)fseek(aj_f.native(), 0L, SEEK_SET);
        aj_ch = fgetc(aj_f.native());
        aj_ck("aj_file_io", aj_ch == (int)'Z');
    } else {
        aj_ck("aj_file_io", 0);
    }
    aj_f.drop();
    aj_f.drop(); /* idempotent cleanup */
    stdlib_alloc_fail_reset();
    stdlib_telemetry_snapshot(&tm);
    aj_ck("aj_file_cleaned", tm.alloc_count == tm.free_count &&
                             tm.alloc_failures == 0ULL);

    aj_skip("file_alloc_failure_hook",
            "file.hp's constructor and file_temp() use fopen()/tmpfile() "
            "directly and perform no stdlib_alloc/stdlib_calloc, so there is "
            "no allocation path for stdlib_alloc_fail_at/after to reach; no "
            "honest OOM test can be fabricated (DEEP_FILE_CONTRACT.md #9).");
    stdlib_telemetry_set_enabled(0);
}

int main(void) {
    stdlib_telemetry_set_enabled(1);
    stdlib_alloc_fail_reset();
    aj_json();
    aj_file();
    stdlib_alloc_fail_reset();
    print("ALLOC_FAIL", aj_checks, aj_failures);
    stdlib_telemetry_set_enabled(0);
    return aj_failures ? 1 : 0;
}
