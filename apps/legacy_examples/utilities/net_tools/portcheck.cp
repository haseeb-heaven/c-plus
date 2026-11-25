/* portcheck.cp -- offline, deterministic TCP/UDP port validator.
 *
 * This is NOT a network client. It never opens a socket, never resolves a
 * name and never contacts a host. It only parses and validates a fixed set of
 * port-number strings supplied as argv tokens, plus a built-in suite of
 * hostile cases whose required refusal is asserted at runtime.
 *
 * Output contract (one line per case, then a summary):
 *   PORT <name> VALID <port>
 *   PORT <name> REFUSED <code> <category> <reason>
 *   PORT <name> FAIL <detail>            (only when an assertion breaks)
 *   PORT_SUMMARY <checks> <valid> <refused> <failed>
 *
 * Refusals are runtime evidence from explicit bounds checks and the checked
 * C+ stdlib, NOT a formal safety proof. All builds use --sanitize=none.
 *
 * Build (from the repository root):
 *   ./build/c+ build apps/legacy_examples/utilities/net_tools/portcheck.cp \
 *       --strict --sanitize=none -I. \
 *       -o apps/legacy_examples/utilities/net_tools/results/portcheck
 */
#include <stddef.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/string.hp"

#define PC_MAX_LEN 5
#define PC_MAX_PORT 65535u

/* argv is an FFI pointer: copy the slot before reading it (--strict). */
static const char *pc_arg(char **argv, int i) {
    const char *v = NULL;
    if (i <= 0)
        return NULL;
    __builtin_memcpy(&v, argv + i, sizeof(v));
    return v;
}

/* Overflow-safe decimal parse. Returns CPLUS_EXIT_OK on success, otherwise a
 * cplus_exit_code value describing the refusal. No division is used: a running
 * value above 6553 cannot be extended by another digit without exceeding
 * 65535, so it is refused before the multiply. */
static int pc_validate(const char *text, unsigned int *out_port,
                       const char **out_reason) {
    string s = string(text);
    size_t n;
    size_t i;
    unsigned int v = 0;

    *out_port = 0;
    *out_reason = "unknown";
    if (!s.valid()) {
        *out_reason = "alloc-failed";
        return CPLUS_EXIT_RESOURCE;
    }
    n = s.size();
    if (n == 0) {
        *out_reason = "empty";
        s.drop();
        return CPLUS_EXIT_UNSUPPORTED;
    }
    if (n > PC_MAX_LEN) {
        *out_reason = "too-long";
        s.drop();
        return CPLUS_EXIT_BOUNDS;
    }
    for (i = 0; i < n; i++) {
        char c = s.at(i);
        unsigned int d;
        if (c < '0' || c > '9') {
            *out_reason = "non-digit";
            s.drop();
            return CPLUS_EXIT_UNSUPPORTED;
        }
        d = (unsigned int)(c - '0');
        if (v > 6553u) {
            *out_reason = "overflow";
            s.drop();
            return CPLUS_EXIT_BOUNDS;
        }
        v = v * 10u + d;
        if (v > PC_MAX_PORT) {
            *out_reason = "overflow";
            s.drop();
            return CPLUS_EXIT_BOUNDS;
        }
    }
    if (n > 1 && s.at(0) == '0') {
        *out_reason = "leading-zero";
        s.drop();
        return CPLUS_EXIT_UNSUPPORTED;
    }
    *out_port = v;
    *out_reason = "ok";
    s.drop();
    return CPLUS_EXIT_OK;
}

static void pc_case(const char *name, const char *value, int expect,
                    int *checks, int *valid, int *refused, int *failed) {
    unsigned int port = 0;
    const char *reason = "unknown";
    int code = pc_validate(value, &port, &reason);

    (*checks)++;
    if (code == CPLUS_EXIT_OK) {
        if (expect != CPLUS_EXIT_OK) {
            print("PORT", name, "FAIL expected-refusal", expect);
            (*failed)++;
        } else {
            print("PORT", name, "VALID", port);
            (*valid)++;
        }
        return;
    }
    {
        error e = error();
        e.set((cplus_exit_code)code, reason);
        if (expect != 0 && expect != code) {
            print("PORT", name, "FAIL wrong-code", code, expect, reason);
            (*failed)++;
        } else {
            print("PORT", name, "REFUSED", code,
                  stdlib_category_name(e.category()), reason);
            (*refused)++;
        }
    }
}

/* Built-in known-answer cases. expect == 0 means "must validate". */
static const char *pc_case_name[] = {
    "v_min",       "v_max",      "v_https",    "h_negative",
    "h_too_big",   "h_huge",     "h_not_num",  "h_trailing",
    "h_empty",     "h_plus",     "h_space",    "h_leading_zero"
};
static const char *pc_case_value[] = {
    "0",           "65535",      "443",        "-1",
    "65536",       "99999999999999999999", "abc", "80x",
    "",            "+80",        " 80",        "00080"
};
static int pc_case_expect[] = {
    CPLUS_EXIT_OK, CPLUS_EXIT_OK, CPLUS_EXIT_OK, CPLUS_EXIT_UNSUPPORTED,
    CPLUS_EXIT_BOUNDS, CPLUS_EXIT_BOUNDS, CPLUS_EXIT_UNSUPPORTED,
    CPLUS_EXIT_UNSUPPORTED, CPLUS_EXIT_UNSUPPORTED,
    CPLUS_EXIT_UNSUPPORTED, CPLUS_EXIT_UNSUPPORTED, CPLUS_EXIT_UNSUPPORTED
};
#define PC_CASE_COUNT (sizeof(pc_case_name) / sizeof(pc_case_name[0]))

int main(int argc, char **argv) {
    int checks = 0;
    int valid = 0;
    int refused = 0;
    int failed = 0;
    int i;

    stdlib_set_error_callback(NULL);
    stdlib_alloc_fail_reset();

    for (i = 0; i < (int)PC_CASE_COUNT; i++)
        pc_case(pc_case_name[i], pc_case_value[i], pc_case_expect[i],
                &checks, &valid, &refused, &failed);
    for (i = 1; i < argc; i++) {
        const char *v = pc_arg(argv, i);
        if (v == NULL) {
            pc_case("argv_null", "", CPLUS_EXIT_UNSUPPORTED,
                    &checks, &valid, &refused, &failed);
        } else {
            pc_case("argv", v, CPLUS_EXIT_OK,
                    &checks, &valid, &refused, &failed);
        }
    }

    print("PORT_SUMMARY", checks, valid, refused, failed);
    return failed == 0 ? 0 : 1;
}
