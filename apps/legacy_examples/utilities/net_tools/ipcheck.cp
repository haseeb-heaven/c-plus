/* ipcheck.cp -- offline, deterministic IPv4 dotted-quad validator.
 *
 * NOT a network client: no socket, no DNS, no host contact. It parses fixed
 * argv tokens and a built-in hostile suite whose required refusal is asserted
 * at runtime. Every byte is read through checked stdlib/string APIs or from
 * bounded local buffers; octet arithmetic refuses before overflow.
 *
 * Output contract (one line per case, then a summary):
 *   IP <name> VALID <o0> <o1> <o2> <o3>
 *   IP <name> REFUSED <code> <category> <reason>
 *   IP <name> FAIL <detail>
 *   IP_SUMMARY <checks> <valid> <refused> <failed>
 *
 * Refusals are runtime evidence from explicit bounds checks and the checked
 * C+ stdlib, NOT a formal safety proof. All builds use --sanitize=none.
 *
 * Build (from the repository root):
 *   ./build/c+ build apps/legacy_examples/utilities/net_tools/ipcheck.cp \
 *       --strict --sanitize=none -I. \
 *       -o apps/legacy_examples/utilities/net_tools/results/ipcheck
 */
#include <stddef.h>
#include <string.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/string.hp"

#define IP_MAX_LEN 15
#define IP_FIELDS 4
#define IP_FIELD_CAP 8

static const char *ip_arg(char **argv, int i) {
    const char *v = NULL;
    if (i <= 0)
        return NULL;
    __builtin_memcpy(&v, argv + i, sizeof(v));
    return v;
}

static int ip_validate(const char *text, unsigned int *out,
                       const char **out_reason) {
    string s = string(text);
    char buf[IP_FIELD_CAP];
    size_t n;
    int f;

    for (f = 0; f < IP_FIELDS; f++)
        out[f] = 0u;
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
    if (n > IP_MAX_LEN) {
        *out_reason = "too-long";
        s.drop();
        return CPLUS_EXIT_BOUNDS;
    }
    if (s.count_fields('.') != (size_t)IP_FIELDS) {
        *out_reason = "wrong-field-count";
        s.drop();
        return CPLUS_EXIT_UNSUPPORTED;
    }
    for (f = 0; f < IP_FIELDS; f++) {
        size_t flen;
        size_t i;
        unsigned int v = 0u;

        if (s.field('.', (size_t)f, buf, sizeof(buf)) != 0) {
            *out_reason = "field-extract";
            s.drop();
            return CPLUS_EXIT_UNSUPPORTED;
        }
        flen = strlen(buf);
        if (flen == 0) {
            *out_reason = "empty-octet";
            s.drop();
            return CPLUS_EXIT_UNSUPPORTED;
        }
        if (flen > 3) {
            *out_reason = "octet-too-long";
            s.drop();
            return CPLUS_EXIT_BOUNDS;
        }
        if (flen > 1 && buf[0] == '0') {
            *out_reason = "leading-zero";
            s.drop();
            return CPLUS_EXIT_UNSUPPORTED;
        }
        for (i = 0; i < flen; i++) {
            unsigned int d;
            if (buf[i] < '0' || buf[i] > '9') {
                *out_reason = "non-digit";
                s.drop();
                return CPLUS_EXIT_UNSUPPORTED;
            }
            d = (unsigned int)(buf[i] - '0');
            if (v > 25u) {
                *out_reason = "octet-overflow";
                s.drop();
                return CPLUS_EXIT_BOUNDS;
            }
            v = v * 10u + d;
            if (v > 255u) {
                *out_reason = "octet-overflow";
                s.drop();
                return CPLUS_EXIT_BOUNDS;
            }
        }
        out[f] = v;
    }
    *out_reason = "ok";
    s.drop();
    return CPLUS_EXIT_OK;
}

static void ip_case(const char *name, const char *value, int expect,
                    int *checks, int *valid, int *refused, int *failed) {
    unsigned int o[IP_FIELDS];
    const char *reason = "unknown";
    int code = ip_validate(value, o, &reason);

    (*checks)++;
    if (code == CPLUS_EXIT_OK) {
        if (expect != CPLUS_EXIT_OK) {
            print("IP", name, "FAIL expected-refusal", expect);
            (*failed)++;
        } else {
            print("IP", name, "VALID", o[0], o[1], o[2], o[3]);
            (*valid)++;
        }
        return;
    }
    {
        error e = error();
        e.set((cplus_exit_code)code, reason);
        if (expect != 0 && expect != code) {
            print("IP", name, "FAIL wrong-code", code, expect, reason);
            (*failed)++;
        } else {
            print("IP", name, "REFUSED", code,
                  stdlib_category_name(e.category()), reason);
            (*refused)++;
        }
    }
}

static const char *ip_case_name[] = {
    "v_max",     "v_zero",     "v_loopback", "h_256",
    "h_3fields", "h_5fields",  "h_empty_oct", "h_leading_zero",
    "h_999",     "h_long",     "h_alpha",     "h_negative",
    "h_empty"
};
static const char *ip_case_value[] = {
    "255.255.255.255", "0.0.0.0", "127.0.0.1", "256.0.0.1",
    "1.2.3",           "1.2.3.4.5", "1..3.4",   "01.2.3.4",
    "1.2.3.999",       "123456789012.1.1.1", "abc.1.1.1", "1.2.3.-4",
    ""
};
static int ip_case_expect[] = {
    CPLUS_EXIT_OK, CPLUS_EXIT_OK, CPLUS_EXIT_OK, CPLUS_EXIT_BOUNDS,
    CPLUS_EXIT_UNSUPPORTED, CPLUS_EXIT_UNSUPPORTED,
    CPLUS_EXIT_UNSUPPORTED, CPLUS_EXIT_UNSUPPORTED,
    CPLUS_EXIT_BOUNDS, CPLUS_EXIT_BOUNDS, CPLUS_EXIT_UNSUPPORTED,
    CPLUS_EXIT_UNSUPPORTED, CPLUS_EXIT_UNSUPPORTED
};
#define IP_CASE_COUNT (sizeof(ip_case_name) / sizeof(ip_case_name[0]))

int main(int argc, char **argv) {
    int checks = 0;
    int valid = 0;
    int refused = 0;
    int failed = 0;
    int i;

    stdlib_set_error_callback(NULL);
    stdlib_alloc_fail_reset();

    for (i = 0; i < (int)IP_CASE_COUNT; i++)
        ip_case(ip_case_name[i], ip_case_value[i], ip_case_expect[i],
                &checks, &valid, &refused, &failed);
    for (i = 1; i < argc; i++) {
        const char *v = ip_arg(argv, i);
        if (v == NULL)
            ip_case("argv_null", "", CPLUS_EXIT_UNSUPPORTED,
                    &checks, &valid, &refused, &failed);
        else
            ip_case("argv", v, CPLUS_EXIT_OK,
                    &checks, &valid, &refused, &failed);
    }

    print("IP_SUMMARY", checks, valid, refused, failed);
    return failed == 0 ? 0 : 1;
}
