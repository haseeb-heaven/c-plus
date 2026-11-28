/* Resource-exhaustion resilience: infinite-loop guards on malformed input.
 *
 * Every malformed-input probe runs in a forked child with its own 2 s alarm and
 * a 5 s parent watchdog.  A child that terminates (with any parse outcome)
 * proves the input did not trap the parser in an unbounded loop; a child killed
 * by its alarm is a FAIL, never a pass.  One positive-control child spins on a
 * volatile counter with NO self-alarm so the parent's watchdog must fire — this
 * proves the guard itself is live rather than silently absent.
 *
 * Inputs are bounded literals/buffers (<= 8192 bytes); nothing is read from an
 * external or unbounded source.  Build needs the same yyjson operands as the
 * other JSON fixtures.
 *
 * Self-checking: "<tag> <1|0>" per check, final
 * "RESOURCE_EXHAUSTION <checks> <failures>". Exit 0 only when failures == 0.
 */
#include <stdio.h>
#include <string.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/json.hp"
#include "stdlib/string.hp"
#include "yyjson.h"
#include "tests/security/resource_exhaustion/support/re_harness.hp"
#include "tests/security/resource_exhaustion/support/re_watchdog.hp"

#define RE_MG_BUFFER 8192

static char re_mg_text[RE_MG_BUFFER];
static volatile int re_mg_spin;

static void re_mg_put(char *out, size_t *pos, char ch) {
    *(out + *pos) = ch;
    *pos = *pos + 1;
}

static void re_mg_spin_child(void) {
    /* Positive control: never returns; only the parent's alarm can stop it. */
    for (;;)
        re_mg_spin++;
}

static void re_mg_json_child(void) {
    error re_mg_err = error();
    json re_mg_doc = json(re_mg_text, &re_mg_err);
    /* Both valid and invalid are acceptable outcomes; the asserted contract is
     * that a bounded parse terminates and, on failure, reports a diagnostic. */
    if (!re_mg_doc.valid())
        re_child_exit(strlen(re_mg_err.what()) > 0 ? 0 : 1);
    re_mg_doc.drop();
    re_child_exit(0);
}

static void re_mg_utf8_child(void) {
    string re_mg_str = string("");
    char re_mg_bad[4];
    int re_mg_i;
    re_mg_bad[0] = (char)0xC3; /* truncated 2-byte sequence */
    re_mg_bad[1] = (char)0x28; /* not a continuation byte */
    re_mg_bad[2] = (char)0x80; /* stray continuation */
    re_mg_bad[3] = '\0';
    for (re_mg_i = 0; re_mg_i < 500; re_mg_i++) {
        (void)re_mg_str.append(re_mg_bad);
        (void)re_mg_str.is_valid_utf8();
        (void)re_mg_str.find("\xFF");
    }
    re_mg_str.drop();
    re_child_exit(0);
}

static void re_mg_digits_child(void) {
    error re_mg_err = error();
    size_t re_mg_pos = 0;
    int re_mg_i;
    re_mg_put(re_mg_text, &re_mg_pos, '[');
    for (re_mg_i = 0; re_mg_i < 4000; re_mg_i++)
        re_mg_put(re_mg_text, &re_mg_pos, '9');
    re_mg_put(re_mg_text, &re_mg_pos, ',');
    re_mg_put(re_mg_text, &re_mg_pos, ']');
    re_mg_put(re_mg_text, &re_mg_pos, '\0');
    {
        json re_mg_doc = json(re_mg_text, &re_mg_err);
        if (!re_mg_doc.valid())
            re_child_exit(strlen(re_mg_err.what()) > 0 ? 0 : 1);
        re_mg_doc.drop();
    }
    re_child_exit(0);
}

int main(void) {
    struct re_watchdog_result re_mg_wd;

    /* ---- 0. positive control: the watchdog really stops a spin --------- */
    (void)!fflush(stdout);
    re_mg_wd = re_run_watchdog(re_mg_spin_child, 2, 0);
    re_check("re_mg_guard_fires", re_mg_wd.timed_out == 1);
    re_check("re_mg_guard_reaps", re_mg_wd.reaped == 1);
    re_check("re_mg_guard_kills", re_mg_wd.signaled == 1 &&
                                  (re_mg_wd.term_signal == 9 ||
                                   re_mg_wd.term_signal == 14));

    /* ---- 1. pathological JSON nesting + bad escape --------------------- */
    {
        size_t re_mg_pos = 0;
        int re_mg_i;
        for (re_mg_i = 0; re_mg_i < 3000; re_mg_i++)
            re_mg_put(re_mg_text, &re_mg_pos, '[');
        re_mg_put(re_mg_text, &re_mg_pos, '"');
        re_mg_put(re_mg_text, &re_mg_pos, '\\');
        re_mg_put(re_mg_text, &re_mg_pos, 'q');
        re_mg_put(re_mg_text, &re_mg_pos, '"');
        re_mg_put(re_mg_text, &re_mg_pos, '\0');
    }
    (void)!fflush(stdout);
    re_mg_wd = re_run_watchdog(re_mg_json_child, 5, 2);
    re_check("re_mg_json_no_hang", re_mg_wd.reaped == 1 &&
                                   re_mg_wd.timed_out == 0 &&
                                   re_mg_wd.exited == 1 &&
                                   re_mg_wd.exit_code == 0);

    /* ---- 2. malformed UTF-8 in a bounded append/find loop -------------- */
    (void)!fflush(stdout);
    re_mg_wd = re_run_watchdog(re_mg_utf8_child, 5, 2);
    re_check("re_mg_utf8_no_hang", re_mg_wd.reaped == 1 &&
                                   re_mg_wd.timed_out == 0 &&
                                   re_mg_wd.exited == 1 &&
                                   re_mg_wd.exit_code == 0);

    /* ---- 3. overlong malformed numeric token --------------------------- */
    (void)!fflush(stdout);
    re_mg_wd = re_run_watchdog(re_mg_digits_child, 5, 2);
    re_check("re_mg_digits_no_hang", re_mg_wd.reaped == 1 &&
                                     re_mg_wd.timed_out == 0 &&
                                     re_mg_wd.exited == 1 &&
                                     re_mg_wd.exit_code == 0);

    re_report();
    return re_harness_failures == 0 ? 0 : 1;
}
