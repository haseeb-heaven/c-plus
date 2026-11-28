/* emailcheck.cp -- offline, deterministic RFC-ish email address validator.
 *
 * NOT a network client: no SMTP, no DNS, no host contact. It parses fixed
 * argv tokens and a built-in hostile suite whose required refusal is asserted
 * at runtime. All input goes through checked string substr() into bounded
 * local buffers; every character test is an explicit range comparison.
 *
 * Output contract (one line per case, then a summary):
 *   EM <name> VALID <local> <domain>
 *   EM <name> REFUSED <code> <category> <reason>
 *   EM <name> FAIL <detail>
 *   EM_SUMMARY <checks> <valid> <refused> <failed>
 *
 * Refusals are runtime evidence from explicit bounds checks and the checked
 * C+ stdlib, NOT a formal safety proof. All builds use --sanitize=none.
 *
 * Build (from the repository root):
 *   ./build/c+ build apps/legacy_examples/utilities/net_tools/emailcheck.cp \
 *       --strict --sanitize=none -I. \
 *       -o apps/legacy_examples/utilities/net_tools/results/emailcheck
 */
#include <stddef.h>
#include <string.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/string.hp"

#define EM_MAX_LEN 96
#define EM_LOCAL_MAX 32
#define EM_DOMAIN_MAX 63

static const char *em_arg(char **argv, int i) {
    const char *v = NULL;
    if (i <= 0)
        return NULL;
    __builtin_memcpy(&v, argv + i, sizeof(v));
    return v;
}

static int em_is_alnum(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9');
}

static int em_is_alpha(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

static int em_validate_local(const char *l, size_t llen,
                             const char **out_reason) {
    size_t i;
    if (llen == 0) {
        *out_reason = "empty-local";
        return CPLUS_EXIT_UNSUPPORTED;
    }
    if (llen > EM_LOCAL_MAX) {
        *out_reason = "local-too-long";
        return CPLUS_EXIT_BOUNDS;
    }
    if (l[0] == '.' || l[llen - 1] == '.') {
        *out_reason = "bad-local-edge";
        return CPLUS_EXIT_UNSUPPORTED;
    }
    for (i = 0; i < llen; i++) {
        char c = l[i];
        if (!(em_is_alnum(c) || c == '.' || c == '_' || c == '%' ||
              c == '+' || c == '-')) {
            *out_reason = "bad-local-char";
            return CPLUS_EXIT_UNSUPPORTED;
        }
        if (c == '.' && i > 0 && l[i - 1] == '.') {
            *out_reason = "consecutive-dot";
            return CPLUS_EXIT_UNSUPPORTED;
        }
    }
    return CPLUS_EXIT_OK;
}

static int em_validate_domain(const char *d, size_t dlen,
                              const char **out_reason) {
    size_t i;
    size_t start = 0;
    size_t last_start = 0;
    size_t last_len = 0;
    int labels = 0;

    if (dlen == 0) {
        *out_reason = "empty-domain";
        return CPLUS_EXIT_UNSUPPORTED;
    }
    if (d[0] == '.' || d[dlen - 1] == '.') {
        *out_reason = "bad-domain-edge";
        return CPLUS_EXIT_UNSUPPORTED;
    }
    for (i = 0; i <= dlen; i++) {
        if (i == dlen || d[i] == '.') {
            size_t len = i - start;
            size_t k;
            if (len == 0) {
                *out_reason = "empty-label";
                return CPLUS_EXIT_UNSUPPORTED;
            }
            if (len > EM_DOMAIN_MAX) {
                *out_reason = "label-too-long";
                return CPLUS_EXIT_BOUNDS;
            }
            if (d[start] == '-' || d[i - 1] == '-') {
                *out_reason = "bad-label-edge";
                return CPLUS_EXIT_UNSUPPORTED;
            }
            for (k = start; k < i; k++) {
                char c = d[k];
                if (!(em_is_alnum(c) || c == '-')) {
                    *out_reason = "bad-domain-char";
                    return CPLUS_EXIT_UNSUPPORTED;
                }
            }
            labels++;
            last_start = start;
            last_len = len;
            start = i + 1;
        }
    }
    if (labels < 2) {
        *out_reason = "no-dot";
        return CPLUS_EXIT_UNSUPPORTED;
    }
    if (last_len < 2) {
        *out_reason = "short-tld";
        return CPLUS_EXIT_UNSUPPORTED;
    }
    for (i = last_start; i < last_start + last_len; i++) {
        if (!em_is_alpha(d[i])) {
            *out_reason = "bad-tld";
            return CPLUS_EXIT_UNSUPPORTED;
        }
    }
    return CPLUS_EXIT_OK;
}

static int em_validate(const char *text, char *local_out, char *domain_out,
                       const char **out_reason) {
    string s = string(text);
    char lbuf[128];
    char dbuf[128];
    size_t n;
    size_t at;
    size_t i;
    int rc;

    local_out[0] = '\0';
    domain_out[0] = '\0';
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
    if (n > EM_MAX_LEN) {
        *out_reason = "too-long";
        s.drop();
        return CPLUS_EXIT_BOUNDS;
    }
    at = (size_t)s.find("@");
    if (s.find("@") < 0) {
        *out_reason = "no-at";
        s.drop();
        return CPLUS_EXIT_UNSUPPORTED;
    }
    if (at == 0) {
        *out_reason = "empty-local";
        s.drop();
        return CPLUS_EXIT_UNSUPPORTED;
    }
    if (at == n - 1) {
        *out_reason = "empty-domain";
        s.drop();
        return CPLUS_EXIT_UNSUPPORTED;
    }
    if (s.substr(0, at, lbuf, sizeof(lbuf)) != 0 ||
        s.substr(at + 1, n - at - 1, dbuf, sizeof(dbuf)) != 0) {
        *out_reason = "extract-failed";
        s.drop();
        return CPLUS_EXIT_UNSUPPORTED;
    }
    for (i = 0; dbuf[i] != '\0'; i++) {
        if (dbuf[i] == '@') {
            *out_reason = "multiple-at";
            s.drop();
            return CPLUS_EXIT_UNSUPPORTED;
        }
    }
    rc = em_validate_local(lbuf, strlen(lbuf), out_reason);
    if (rc != CPLUS_EXIT_OK) {
        s.drop();
        return rc;
    }
    rc = em_validate_domain(dbuf, strlen(dbuf), out_reason);
    if (rc != CPLUS_EXIT_OK) {
        s.drop();
        return rc;
    }
    for (i = 0; lbuf[i] != '\0'; i++)
        local_out[i] = lbuf[i];
    local_out[strlen(lbuf)] = '\0';
    for (i = 0; dbuf[i] != '\0'; i++)
        domain_out[i] = dbuf[i];
    domain_out[strlen(dbuf)] = '\0';
    s.drop();
    return CPLUS_EXIT_OK;
}

static void em_case(const char *name, const char *value, int expect,
                    int *checks, int *valid, int *refused, int *failed) {
    char local[128];
    char domain[128];
    const char *reason = "unknown";
    int code = em_validate(value, local, domain, &reason);

    (*checks)++;
    if (code == CPLUS_EXIT_OK) {
        if (expect != CPLUS_EXIT_OK) {
            print("EM", name, "FAIL expected-refusal", expect);
            (*failed)++;
        } else {
            print("EM", name, "VALID", local, domain);
            (*valid)++;
        }
        return;
    }
    {
        error e = error();
        e.set((cplus_exit_code)code, reason);
        if (expect != 0 && expect != code) {
            print("EM", name, "FAIL wrong-code", code, expect, reason);
            (*failed)++;
        } else {
            print("EM", name, "REFUSED", code,
                  stdlib_category_name(e.category()), reason);
            (*refused)++;
        }
    }
}

static const char *em_case_name[] = {
    "v_simple",     "v_plus_tag",   "v_subdomain", "h_no_at",
    "h_double_at",  "h_empty_local", "h_empty_dom", "h_dotdot",
    "h_leading_dot", "h_no_dot",    "h_short_tld", "h_bad_label",
    "h_long_local", "h_long_total"
};
static const char *em_case_value[] = {
    "alice@example.com",
    "a.b+tag@sub.example.co.uk",
    "x@y.io",
    "no-at-sign",
    "a@@b.com",
    "@example.com",
    "alice@",
    "a..b@example.com",
    ".alice@example.com",
    "alice@example",
    "alice@example.c",
    "alice@-example.com",
    "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa@example.com",
    "aaaaaaaaaa" "aaaaaaaaaa" "aaaaaaaaaa" "aaaaaaaaaa" "aaaaaaaaaa"
    "aaaaaaaaaa" "aaaaaaaaaa" "aaaaaaaaaa" "aaaaaaaaaa" "aaaaaaaaaa"
};
static int em_case_expect[] = {
    CPLUS_EXIT_OK, CPLUS_EXIT_OK, CPLUS_EXIT_OK, CPLUS_EXIT_UNSUPPORTED,
    CPLUS_EXIT_UNSUPPORTED, CPLUS_EXIT_UNSUPPORTED,
    CPLUS_EXIT_UNSUPPORTED, CPLUS_EXIT_UNSUPPORTED,
    CPLUS_EXIT_UNSUPPORTED, CPLUS_EXIT_UNSUPPORTED,
    CPLUS_EXIT_UNSUPPORTED, CPLUS_EXIT_UNSUPPORTED,
    CPLUS_EXIT_BOUNDS, CPLUS_EXIT_BOUNDS
};
#define EM_CASE_COUNT (sizeof(em_case_name) / sizeof(em_case_name[0]))

int main(int argc, char **argv) {
    int checks = 0;
    int valid = 0;
    int refused = 0;
    int failed = 0;
    int i;

    stdlib_set_error_callback(NULL);
    stdlib_alloc_fail_reset();

    for (i = 0; i < (int)EM_CASE_COUNT; i++)
        em_case(em_case_name[i], em_case_value[i], em_case_expect[i],
                &checks, &valid, &refused, &failed);
    for (i = 1; i < argc; i++) {
        const char *v = em_arg(argv, i);
        if (v == NULL)
            em_case("argv_null", "", CPLUS_EXIT_UNSUPPORTED,
                    &checks, &valid, &refused, &failed);
        else
            em_case("argv", v, CPLUS_EXIT_OK,
                    &checks, &valid, &refused, &failed);
    }

    print("EM_SUMMARY", checks, valid, refused, failed);
    return failed == 0 ? 0 : 1;
}
