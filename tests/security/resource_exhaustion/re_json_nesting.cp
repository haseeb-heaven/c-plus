/* Resource-exhaustion resilience: excessive JSON nesting.
 *
 * DOCUMENTED LIMITATION (important): the vendored yyjson has no explicit
 * reader depth limit in this build.  third_party/yyjson/yyjson.h:113-117
 * defines YYJSON_READER_DEPTH_LIMIT to 0 ("0 disables the policy limit"), and
 * libyyjson.a in build/ext was compiled with that default.  stdlib/json.hp
 * calls yyjson_read_opts(..., YYJSON_READ_NOFLAG, NULL, &info), so it neither
 * sets a limit nor exposes one.  There is therefore NO yyjson depth limit to
 * assert here.  This fixture imposes its own fixed caps (1024 balanced levels,
 * 4096 unterminated levels) and runs each parse in a forked child under a
 * 5 s parent watchdog / 2 s child alarm.  It asserts resilience: a bounded
 * parse completes, malformed over-deep input is refused or at worst contained,
 * and neither case hangs or crashes.  It does NOT claim a parser depth bound.
 *
 * Build: c+ build this.cp --strict --sanitize=none \
 *          -Ithird_party/yyjson -Lbuild/ext -lyyjson -o OUT
 *
 * Self-checking: "<tag> <1|0>" per check, final
 * "RESOURCE_EXHAUSTION <checks> <failures>". Exit 0 only when failures == 0.
 */
#include <stdio.h>
#include <string.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/json.hp"
#include "yyjson.h"
#include "tests/security/resource_exhaustion/support/re_harness.hp"
#include "tests/security/resource_exhaustion/support/re_watchdog.hp"

#define RE_JN_DEEP 1024
#define RE_JN_UNCLOSED 4096
#define RE_JN_BUF ((RE_JN_UNCLOSED * 2) + 8)

static char re_jn_text[RE_JN_BUF];
static int re_jn_child_failures = 0;

static void re_jn_silent(int cond) {
    if (!cond)
        re_jn_child_failures++;
}

static void re_jn_put(char *out, size_t *pos, char ch) {
    *(out + *pos) = ch;
    *pos = *pos + 1;
}

/* Balanced nested arrays: [[[...0...]]] with `levels` opening brackets. */
static void re_jn_build_balanced(char *out, int levels) {
    size_t re_jn_pos = 0;
    int re_jn_i;
    for (re_jn_i = 0; re_jn_i < levels; re_jn_i++)
        re_jn_put(out, &re_jn_pos, '[');
    re_jn_put(out, &re_jn_pos, '0');
    for (re_jn_i = 0; re_jn_i < levels; re_jn_i++)
        re_jn_put(out, &re_jn_pos, ']');
    re_jn_put(out, &re_jn_pos, '\0');
}

/* Unterminated nested arrays: a malformed, resource-hungry prefix. */
static void re_jn_build_unclosed(char *out, int levels) {
    size_t re_jn_pos = 0;
    int re_jn_i;
    for (re_jn_i = 0; re_jn_i < levels; re_jn_i++)
        re_jn_put(out, &re_jn_pos, '[');
    re_jn_put(out, &re_jn_pos, '\0');
}

static void re_jn_balanced_child(void) {
    error re_jn_err = error();
    json re_jn_doc = json(re_jn_text, &re_jn_err);
    int re_jn_ok = re_jn_doc.valid() == 1;
    if (re_jn_ok) {
        json_view re_jn_cur = re_jn_doc.root();
        int re_jn_i;
        for (re_jn_i = 0; re_jn_i < RE_JN_DEEP; re_jn_i++) {
            re_jn_silent(re_jn_cur.is_array() == 1 && re_jn_cur.size() == 1);
            re_jn_cur = re_jn_cur.at(0);
        }
        re_jn_silent(re_jn_cur.is_number() == 1);
        re_jn_doc.drop();
    }
    re_jn_silent(re_jn_ok);
    re_child_exit(re_jn_child_failures == 0 ? 0 : 1);
}

static void re_jn_unclosed_child(void) {
    error re_jn_err = error();
    json re_jn_doc = json(re_jn_text, &re_jn_err);
    /* Unterminated containers are invalid JSON at any depth; assert a clean
     * refusal with a non-empty diagnostic, and never a crash or a hang. */
    re_jn_silent(re_jn_doc.valid() == 0);
    re_jn_silent(re_jn_err.failed_now() == 1);
    re_jn_silent(strlen(re_jn_err.what()) > 0);
    re_jn_doc.drop();
    re_child_exit(re_jn_child_failures == 0 ? 0 : 1);
}

int main(void) {
    struct re_watchdog_result re_jn_wd;

    /* Declare the absence of an explicit yyjson depth limit in the output. */
    print("RE_JN_READER_DEPTH_LIMIT", (int)YYJSON_READER_DEPTH_LIMIT);
    /* Pinned to the vendored default (0 = no explicit policy limit).  If the
     * dependency is rebuilt with a limit, update this expectation deliberately. */
    re_check("re_jn_limit_documented", YYJSON_READER_DEPTH_LIMIT == 0);

    (void)!fflush(stdout);
    re_jn_build_balanced(re_jn_text, RE_JN_DEEP);
    re_jn_wd = re_run_watchdog(re_jn_balanced_child, 5, 2);
    re_check("re_jn_balanced_reaped", re_jn_wd.reaped == 1);
    re_check("re_jn_balanced_no_watchdog", re_jn_wd.timed_out == 0);
    re_check("re_jn_balanced_exited", re_jn_wd.exited == 1 &&
                                     re_jn_wd.exit_code == 0);

    (void)!fflush(stdout);
    re_jn_build_unclosed(re_jn_text, RE_JN_UNCLOSED);
    re_jn_wd = re_run_watchdog(re_jn_unclosed_child, 5, 2);
    re_check("re_jn_unclosed_reaped", re_jn_wd.reaped == 1);
    re_check("re_jn_unclosed_no_watchdog", re_jn_wd.timed_out == 0);
    re_check("re_jn_unclosed_exited", re_jn_wd.exited == 1 &&
                                     re_jn_wd.exit_code == 0);

    re_report();
    return re_harness_failures == 0 ? 0 : 1;
}
