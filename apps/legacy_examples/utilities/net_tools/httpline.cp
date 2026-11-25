/* httpline.cp -- offline, deterministic HTTP request-line validator.
 *
 * NOT a network client: it never reads a socket or sends a request. It parses
 * fixed argv tokens of the form "METHOD path VERSION" plus a built-in hostile
 * suite whose required refusal is asserted at runtime. Input is copied through
 * checked string field() into bounded local buffers before inspection.
 *
 * Output contract (one line per case, then a summary):
 *   HTTP <name> VALID <method> <path> <version>
 *   HTTP <name> REFUSED <code> <category> <reason>
 *   HTTP <name> FAIL <detail>
 *   HTTP_SUMMARY <checks> <valid> <refused> <failed>
 *
 * Refusals are runtime evidence from explicit bounds checks and the checked
 * C+ stdlib, NOT a formal safety proof. All builds use --sanitize=none.
 *
 * Build (from the repository root):
 *   ./build/c+ build examples/utilities/net_tools/httpline.cp \
 *       --strict --sanitize=none -I. \
 *       -o examples/utilities/net_tools/results/httpline
 */
#include <stddef.h>
#include <string.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/string.hp"

#define HL_MAX_LEN 128
#define HL_MAX_PATH 80
#define HL_MBUF 16
#define HL_PBUF 96
#define HL_VBUF 16

static const char *hl_arg(char **argv, int i) {
    const char *v = NULL;
    if (i <= 0)
        return NULL;
    __builtin_memcpy(&v, argv + i, sizeof(v));
    return v;
}

static int hl_known_method(const char *m) {
    static const char *methods[] = {
        "GET", "POST", "PUT", "DELETE", "HEAD", "OPTIONS", "PATCH"
    };
    size_t i;
    for (i = 0; i < sizeof(methods) / sizeof(methods[0]); i++)
        if (strcmp(m, methods[i]) == 0)
            return 1;
    return 0;
}

static int hl_known_version(const char *v) {
    return strcmp(v, "HTTP/1.0") == 0 || strcmp(v, "HTTP/1.1") == 0;
}

static int hl_validate(const char *text, char *method_out, char *path_out,
                       char *version_out, const char **out_reason) {
    string s = string(text);
    char m[HL_MBUF];
    char p[HL_PBUF];
    char v[HL_VBUF];
    size_t n;
    size_t plen;
    size_t i;

    method_out[0] = '\0';
    path_out[0] = '\0';
    version_out[0] = '\0';
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
    if (n > HL_MAX_LEN) {
        *out_reason = "too-long";
        s.drop();
        return CPLUS_EXIT_BOUNDS;
    }
    if (s.count_fields(' ') != 3) {
        *out_reason = "wrong-field-count";
        s.drop();
        return CPLUS_EXIT_UNSUPPORTED;
    }
    if (s.field(' ', 0, m, sizeof(m)) != 0 ||
        s.field(' ', 1, p, sizeof(p)) != 0 ||
        s.field(' ', 2, v, sizeof(v)) != 0) {
        *out_reason = "field-extract";
        s.drop();
        return CPLUS_EXIT_UNSUPPORTED;
    }
    if (m[0] == '\0' || p[0] == '\0' || v[0] == '\0') {
        *out_reason = "empty-field";
        s.drop();
        return CPLUS_EXIT_UNSUPPORTED;
    }
    if (!hl_known_method(m)) {
        *out_reason = "bad-method";
        s.drop();
        return CPLUS_EXIT_UNSUPPORTED;
    }
    if (!hl_known_version(v)) {
        *out_reason = "bad-version";
        s.drop();
        return CPLUS_EXIT_UNSUPPORTED;
    }
    plen = strlen(p);
    if (plen > HL_MAX_PATH) {
        *out_reason = "path-too-long";
        s.drop();
        return CPLUS_EXIT_BOUNDS;
    }
    if (p[0] != '/') {
        *out_reason = "path-not-absolute";
        s.drop();
        return CPLUS_EXIT_UNSUPPORTED;
    }
    for (i = 0; i < plen; i++) {
        unsigned char c = (unsigned char)p[i];
        if (c < 0x21 || c > 0x7e) {
            *out_reason = "bad-path-char";
            s.drop();
            return CPLUS_EXIT_UNSUPPORTED;
        }
        if (c == '.' && i > 0 && p[i - 1] == '.') {
            *out_reason = "dot-dot-path";
            s.drop();
            return CPLUS_EXIT_UNSUPPORTED;
        }
    }
    for (i = 0; m[i] != '\0'; i++)
        method_out[i] = m[i];
    method_out[strlen(m)] = '\0';
    for (i = 0; p[i] != '\0'; i++)
        path_out[i] = p[i];
    path_out[plen] = '\0';
    for (i = 0; v[i] != '\0'; i++)
        version_out[i] = v[i];
    version_out[strlen(v)] = '\0';
    s.drop();
    return CPLUS_EXIT_OK;
}

static void hl_case(const char *name, const char *value, int expect,
                    int *checks, int *valid, int *refused, int *failed) {
    char method[HL_MBUF];
    char path[HL_PBUF];
    char version[HL_VBUF];
    const char *reason = "unknown";
    int code = hl_validate(value, method, path, version, &reason);

    (*checks)++;
    if (code == CPLUS_EXIT_OK) {
        if (expect != CPLUS_EXIT_OK) {
            print("HTTP", name, "FAIL expected-refusal", expect);
            (*failed)++;
        } else {
            print("HTTP", name, "VALID", method, path, version);
            (*valid)++;
        }
        return;
    }
    {
        error e = error();
        e.set((cplus_exit_code)code, reason);
        if (expect != 0 && expect != code) {
            print("HTTP", name, "FAIL wrong-code", code, expect, reason);
            (*failed)++;
        } else {
            print("HTTP", name, "REFUSED", code,
                  stdlib_category_name(e.category()), reason);
            (*refused)++;
        }
    }
}

static const char *hl_case_name[] = {
    "v_get",        "v_post",       "v_head",        "h_bad_method",
    "h_bad_ver",    "h_two_fields", "h_four_fields", "h_double_space",
    "h_missing_path", "h_relative", "h_traversal",   "h_trailing_space",
    "h_oversized"
};
static const char *hl_case_value[] = {
    "GET /index.html HTTP/1.1",
    "POST /submit HTTP/1.0",
    "HEAD / HTTP/1.1",
    "BREW /x HTTP/1.1",
    "GET /x HTTP/2.0",
    "GET /x",
    "GET /x HTTP/1.1 extra",
    "GET  /x HTTP/1.1",
    "GET  HTTP/1.1",
    "GET x HTTP/1.1",
    "GET /../etc/passwd HTTP/1.1",
    "GET /x HTTP/1.1 ",
    "GET /" "xxxxxxxxxx" "xxxxxxxxxx" "xxxxxxxxxx" "xxxxxxxxxx"
    "xxxxxxxxxx" "xxxxxxxxxx" "xxxxxxxxxx" "xxxxxxxxxx" "xxxxxxxxxx"
    "xxxxxxxxxx" "xxxxxxxxxx" "xxxxxxxxxx" " HTTP/1.1"
};
static int hl_case_expect[] = {
    CPLUS_EXIT_OK, CPLUS_EXIT_OK, CPLUS_EXIT_OK, CPLUS_EXIT_UNSUPPORTED,
    CPLUS_EXIT_UNSUPPORTED, CPLUS_EXIT_UNSUPPORTED,
    CPLUS_EXIT_UNSUPPORTED, CPLUS_EXIT_UNSUPPORTED,
    CPLUS_EXIT_UNSUPPORTED, CPLUS_EXIT_UNSUPPORTED,
    CPLUS_EXIT_UNSUPPORTED, CPLUS_EXIT_UNSUPPORTED,
    CPLUS_EXIT_BOUNDS
};
#define HL_CASE_COUNT (sizeof(hl_case_name) / sizeof(hl_case_name[0]))

int main(int argc, char **argv) {
    int checks = 0;
    int valid = 0;
    int refused = 0;
    int failed = 0;
    int i;

    stdlib_set_error_callback(NULL);
    stdlib_alloc_fail_reset();

    for (i = 0; i < (int)HL_CASE_COUNT; i++)
        hl_case(hl_case_name[i], hl_case_value[i], hl_case_expect[i],
                &checks, &valid, &refused, &failed);
    for (i = 1; i < argc; i++) {
        const char *v = hl_arg(argv, i);
        if (v == NULL)
            hl_case("argv_null", "", CPLUS_EXIT_UNSUPPORTED,
                    &checks, &valid, &refused, &failed);
        else
            hl_case("argv", v, CPLUS_EXIT_OK,
                    &checks, &valid, &refused, &failed);
    }

    print("HTTP_SUMMARY", checks, valid, refused, failed);
    return failed == 0 ? 0 : 1;
}
