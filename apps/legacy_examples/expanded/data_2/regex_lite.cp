/* regex_lite.cp -- deterministic wildcard matcher with a backtracking guard.
 *
 * NOT an interactive tool.  All input is fixed argv (or a built-in fixed
 * table).  No clock, no network, no user paths, no shelling out.  Identical
 * argv yields byte-identical stdout.
 *
 * Build (from the repository root):
 *   ./build/c+ build examples/expanded/data_2/regex_lite.cp \
 *       --strict --sanitize=none -I. -o examples/expanded/data_2/.run/regex_lite
 *
 * Usage:
 *   regex_lite                 run the built-in fixed pattern/text table
 *   regex_lite <pattern> <text>  match one fixed pair
 *
 * Wildcards: '*' matches zero or more bytes, '?' matches exactly one byte.
 * The classic iterative greedy-with-backtrack matcher is bounded by a hard
 * step budget (256).  Oversized inputs, invalid characters and step-budget
 * exhaustion are REFUSED (exit 2); a refusal is never counted as a match.
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/vector.hp"
#include "stdlib/string.hp"

#define RG_MAX_PAT 64
#define RG_MAX_TEXT 128
#define RG_MAX_STEPS 256
#define RG_CASES 6

static const char *rg_patterns[RG_CASES] = {
    "*", "a?c", "a?c", "a*b", "a*b", "a*b?c"
};
static const char *rg_texts[RG_CASES] = {
    "anything", "abc", "ac", "aaab", "aaac", "aaabcc"
};
static const int rg_want[RG_CASES] = { 1, 1, 0, 1, 0, 1 };

static const char *rg_arg(char **argv, int i) {
    const char *v = NULL;
    if (i <= 0)
        return NULL;
    __builtin_memcpy(&v, argv + i, sizeof(v));
    return v;
}

static int rg_refuse(const char *app, const char *reason, cplus_exit_code code) {
    error e = error();
    e.set(code, reason);
    printf("%s error code=%d reason=%s\n", app, e.code(), reason);
    return 2;
}

/* -1 too long, -2 empty, -3 invalid byte. */
static int rg_validate(const char *text, size_t limit, size_t *out_len,
                       int pattern) {
    string s = string(text);
    size_t n = s.size();
    size_t i;
    if (n == 0)
        return -2;
    if (n > limit)
        return -1;
    for (i = 0; i < n; i++) {
        unsigned char c = (unsigned char)s.at(i);
        if (pattern) {
            int ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                     (c >= '0' && c <= '9') || c == '*' || c == '?' ||
                     c == '.' || c == '-' || c == '_';
            if (!ok)
                return -3;
        } else if (c < 0x21 || c > 0x7e) {
            return -3;
        }
    }
    *out_len = n;
    return 0;
}

/* Returns 1 match, 0 no-match, -1 budget exhausted.  Number of executed steps
 * and backtracks are reported through out-parameters for the transcript. */
static int rg_match(const char *pattern, const char *text, int *steps_out,
                    int *backs_out) {
    string p = string(pattern);
    string t = string(text);
    size_t m = p.size();
    size_t n = t.size();
    size_t pi = 0;
    size_t ti = 0;
    size_t star_p = 0;
    size_t star_t = 0;
    int has_star = 0;
    int steps = 0;
    int backs = 0;
    int matched = 1;
    while (ti < n) {
        steps++;
        if (steps > RG_MAX_STEPS)
            return -1;
        if (pi < m && (p.at(pi) == t.at(ti) || p.at(pi) == '?')) {
            pi++;
            ti++;
        } else if (pi < m && p.at(pi) == '*') {
            star_p = pi;
            star_t = ti;
            has_star = 1;
            pi++;
        } else if (has_star) {
            backs++;
            steps++;
            if (steps > RG_MAX_STEPS)
                return -1;
            star_t = star_t + 1;
            ti = star_t;
            pi = star_p + 1;
        } else {
            matched = 0;
            break;
        }
    }
    while (pi < m && p.at(pi) == '*') {
        steps++;
        if (steps > RG_MAX_STEPS)
            return -1;
        pi++;
    }
    if (pi != m)
        matched = 0;
    *steps_out = steps;
    *backs_out = backs;
    return matched ? 1 : 0;
}

static int rg_run_pair(const char *pat, const char *txt) {
    size_t plen = 0;
    size_t tlen = 0;
    int r;
    int steps = 0;
    int backs = 0;
    int result;
    r = rg_validate(pat, RG_MAX_PAT, &plen, 1);
    if (r == -1)
        return rg_refuse("REGEX", "pattern-too-long", CPLUS_EXIT_BOUNDS);
    if (r == -3)
        return rg_refuse("REGEX", "invalid-pattern", CPLUS_EXIT_UNSUPPORTED);
    r = rg_validate(txt, RG_MAX_TEXT, &tlen, 0);
    if (r == -1)
        return rg_refuse("REGEX", "text-too-long", CPLUS_EXIT_BOUNDS);
    if (r == -3)
        return rg_refuse("REGEX", "invalid-text", CPLUS_EXIT_UNSUPPORTED);
    if (r == -2 || plen == 0 || tlen == 0)
        return rg_refuse("REGEX", "empty-input", CPLUS_EXIT_UNSUPPORTED);

    result = rg_match(pat, txt, &steps, &backs);
    if (result == -1)
        return rg_refuse("REGEX", "step-budget-exhausted", CPLUS_EXIT_RESOURCE);
    printf("REGEX pattern=%s text=%s match=%d steps=%d backtracks=%d "
           "plen=%d tlen=%d\n", pat, txt, result, steps, backs,
           (int)plen, (int)tlen);
    return 0;
}

int main(int argc, char **argv) {
    if (argc == 1) {
        int got[RG_CASES];
        int steps[RG_CASES];
        int backs[RG_CASES];
        int i;
        for (i = 0; i < RG_CASES; i++) {
            int r = rg_match(rg_patterns[i], rg_texts[i],
                             &steps[i], &backs[i]);
            if (r == -1)
                return rg_refuse("REGEX", "step-budget-exhausted",
                                 CPLUS_EXIT_RESOURCE);
            got[i] = r;
        }
        for (i = 0; i < RG_CASES; i++)
            if (got[i] != rg_want[i])
                return rg_refuse("REGEX", "selfcheck-failed",
                                 CPLUS_EXIT_UNSUPPORTED);
        printf("REGEX app=regex_lite max_pat=%d max_text=%d max_steps=%d "
               "selfcheck=ok cases=%d\n",
               RG_MAX_PAT, RG_MAX_TEXT, RG_MAX_STEPS, RG_CASES);
        for (i = 0; i < RG_CASES; i++)
            printf("REGEX case %d pattern=%s text=%s match=%d want=%d "
                   "ok=1 steps=%d backtracks=%d\n",
                   i, rg_patterns[i], rg_texts[i], got[i], rg_want[i],
                   steps[i], backs[i]);
        printf("REGEX done cases=%d\n", RG_CASES);
        return 0;
    }
    if (argc == 3) {
        const char *pat = rg_arg(argv, 1);
        const char *txt = rg_arg(argv, 2);
        int r;
        if (pat == NULL || txt == NULL)
            return rg_refuse("REGEX", "null-arg", CPLUS_EXIT_NULL_POINTER);
        r = rg_run_pair(pat, txt);
        if (r != 0)
            return r;
        printf("REGEX done\n");
        return 0;
    }
    return rg_refuse("REGEX", "usage", CPLUS_EXIT_UNSUPPORTED);
}
