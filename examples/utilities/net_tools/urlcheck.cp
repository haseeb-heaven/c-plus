/* urlcheck.cp -- offline, deterministic URL shape validator.
 *
 * NOT a network client: it never resolves a name, opens a socket or fetches a
 * resource. It only parses fixed argv tokens and a built-in hostile suite
 * whose required refusal is asserted at runtime. All bytes are read through
 * checked string substr()/find() APIs or from bounded local buffers.
 *
 * Grammar accepted: scheme "://" host [":" port] ["/" path]
 *   scheme = http | https
 *   host   = 1..63 alnum/'.'/'-' bytes, not starting/ending with '.' or '-'
 *   port   = 1..65535 decimal (no leading zeros, no sign, no whitespace)
 *   path   = 0..64 printable-non-space bytes starting with '/', no ".."
 *
 * Output contract (one line per case, then a summary):
 *   URL <name> VALID <scheme> <host> <port> <path> <port-kind>
 *   URL <name> REFUSED <code> <category> <reason>
 *   URL <name> FAIL <detail>
 *   URL_SUMMARY <checks> <valid> <refused> <failed>
 *
 * Refusals are runtime evidence from explicit bounds checks and the checked
 * C+ stdlib, NOT a formal safety proof. All builds use --sanitize=none.
 *
 * Build (from the repository root):
 *   ./build/c+ build examples/utilities/net_tools/urlcheck.cp \
 *       --strict --sanitize=none -I. \
 *       -o examples/utilities/net_tools/results/urlcheck
 */
#include <stddef.h>
#include <string.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/string.hp"

#define URL_MAX_LEN 128
#define URL_SCHEME_MAX 8
#define URL_HOST_MAX 63
#define URL_PORT_MAX_LEN 5
#define URL_PATH_MAX 64

static const char *url_arg(char **argv, int i) {
    const char *v = NULL;
    if (i <= 0)
        return NULL;
    __builtin_memcpy(&v, argv + i, sizeof(v));
    return v;
}

static long uc_find_char(const char *s, size_t n, char target) {
    size_t i;
    for (i = 0; i < n; i++)
        if (s[i] == target)
            return (long)i;
    return -1;
}

static size_t uc_copy_range(const char *src, size_t start, size_t len,
                            char *dst, size_t cap) {
    size_t i;
    if (cap == 0)
        return 0;
    if (len + 1 > cap)
        len = cap - 1;
    for (i = 0; i < len; i++)
        dst[i] = src[start + i];
    dst[len] = '\0';
    return len;
}

static int uc_is_alnum(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9');
}

static int uc_validate(const char *text, char *scheme_out, char *host_out,
                       unsigned int *port_out, int *explicit_out,
                       char *path_out, const char **out_reason) {
    string s = string(text);
    char rbuf[URL_MAX_LEN + 2];
    char abuf[URL_MAX_LEN + 2];
    char schar[URL_SCHEME_MAX + 2];
    char pbuf[URL_PATH_MAX + 2];
    char pb[URL_PORT_MAX_LEN + 4];
    size_t n;
    size_t scheme_len;
    size_t rest_start;
    size_t rest_len;
    size_t rest_n;
    long sep;
    long slash;
    long colon;
    size_t auth_len;
    size_t host_len;
    size_t port_len;
    unsigned int port = 0u;
    int explicit_port = 0;
    size_t i;

    scheme_out[0] = '\0';
    host_out[0] = '\0';
    path_out[0] = '\0';
    *port_out = 0u;
    *explicit_out = 0;
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
    if (n > URL_MAX_LEN) {
        *out_reason = "too-long";
        s.drop();
        return CPLUS_EXIT_BOUNDS;
    }
    sep = s.find("://");
    if (sep < 0) {
        *out_reason = "no-scheme";
        s.drop();
        return CPLUS_EXIT_UNSUPPORTED;
    }
    scheme_len = (size_t)sep;
    if (scheme_len == 0) {
        *out_reason = "empty-scheme";
        s.drop();
        return CPLUS_EXIT_UNSUPPORTED;
    }
    if (scheme_len > URL_SCHEME_MAX) {
        *out_reason = "scheme-too-long";
        s.drop();
        return CPLUS_EXIT_BOUNDS;
    }
    if (s.substr(0, scheme_len, schar, sizeof(schar)) != 0) {
        *out_reason = "extract-failed";
        s.drop();
        return CPLUS_EXIT_UNSUPPORTED;
    }
    if (strcmp(schar, "http") != 0 && strcmp(schar, "https") != 0) {
        *out_reason = "bad-scheme";
        s.drop();
        return CPLUS_EXIT_UNSUPPORTED;
    }
    rest_start = scheme_len + 3;
    rest_len = n - rest_start;
    if (s.substr(rest_start, rest_len, rbuf, sizeof(rbuf)) != 0) {
        *out_reason = "extract-failed";
        s.drop();
        return CPLUS_EXIT_UNSUPPORTED;
    }
    rest_n = strlen(rbuf);

    slash = uc_find_char(rbuf, rest_n, '/');
    if (slash >= 0) {
        auth_len = (size_t)slash;
        if (rest_n - auth_len > URL_PATH_MAX) {
            *out_reason = "path-too-long";
            s.drop();
            return CPLUS_EXIT_BOUNDS;
        }
        uc_copy_range(rbuf, auth_len, rest_n - auth_len, pbuf, sizeof(pbuf));
    } else {
        auth_len = rest_n;
        pbuf[0] = '/';
        pbuf[1] = '\0';
    }
    for (i = 0; pbuf[i] != '\0'; i++) {
        unsigned char c = (unsigned char)pbuf[i];
        if (c < 0x21 || c > 0x7e) {
            *out_reason = "bad-path-char";
            s.drop();
            return CPLUS_EXIT_UNSUPPORTED;
        }
        if (c == '.' && i > 0 && pbuf[i - 1] == '.') {
            *out_reason = "dot-dot-path";
            s.drop();
            return CPLUS_EXIT_UNSUPPORTED;
        }
    }

    if (auth_len == 0) {
        *out_reason = "empty-authority";
        s.drop();
        return CPLUS_EXIT_UNSUPPORTED;
    }
    uc_copy_range(rbuf, 0, auth_len, abuf, sizeof(abuf));
    colon = uc_find_char(abuf, auth_len, ':');
    if (colon >= 0) {
        host_len = (size_t)colon;
        port_len = auth_len - host_len - 1;
        if (port_len == 0) {
            *out_reason = "empty-port";
            s.drop();
            return CPLUS_EXIT_UNSUPPORTED;
        }
        if (port_len > URL_PORT_MAX_LEN) {
            *out_reason = "port-too-long";
            s.drop();
            return CPLUS_EXIT_BOUNDS;
        }
        uc_copy_range(abuf, host_len + 1, port_len, pb, sizeof(pb));
        if (uc_find_char(pb, port_len, ':') >= 0) {
            *out_reason = "bad-authority";
            s.drop();
            return CPLUS_EXIT_UNSUPPORTED;
        }
        {
            unsigned int v = 0u;
            for (i = 0; i < port_len; i++) {
                unsigned int d;
                if (pb[i] < '0' || pb[i] > '9') {
                    *out_reason = "bad-port";
                    s.drop();
                    return CPLUS_EXIT_UNSUPPORTED;
                }
                d = (unsigned int)(pb[i] - '0');
                if (v > 6553u) {
                    *out_reason = "port-overflow";
                    s.drop();
                    return CPLUS_EXIT_BOUNDS;
                }
                v = v * 10u + d;
                if (v > 65535u) {
                    *out_reason = "port-overflow";
                    s.drop();
                    return CPLUS_EXIT_BOUNDS;
                }
            }
            if (v == 0u) {
                *out_reason = "port-zero";
                s.drop();
                return CPLUS_EXIT_UNSUPPORTED;
            }
            port = v;
        }
        explicit_port = 1;
    } else {
        host_len = auth_len;
        port = strcmp(schar, "https") == 0 ? 443u : 80u;
        explicit_port = 0;
    }

    if (host_len == 0) {
        *out_reason = "empty-host";
        s.drop();
        return CPLUS_EXIT_UNSUPPORTED;
    }
    if (host_len > URL_HOST_MAX) {
        *out_reason = "host-too-long";
        s.drop();
        return CPLUS_EXIT_BOUNDS;
    }
    if (abuf[0] == '.' || abuf[0] == '-' ||
        abuf[host_len - 1] == '.' || abuf[host_len - 1] == '-') {
        *out_reason = "bad-host-edge";
        s.drop();
        return CPLUS_EXIT_UNSUPPORTED;
    }
    for (i = 0; i < host_len; i++) {
        char c = abuf[i];
        if (!(uc_is_alnum(c) || c == '.' || c == '-')) {
            *out_reason = "bad-host-char";
            s.drop();
            return CPLUS_EXIT_UNSUPPORTED;
        }
        if (c == '.' && i > 0 && abuf[i - 1] == '.') {
            *out_reason = "consecutive-dot";
            s.drop();
            return CPLUS_EXIT_UNSUPPORTED;
        }
    }

    uc_copy_range(schar, 0, scheme_len, scheme_out,
                  (size_t)URL_SCHEME_MAX + 2);
    uc_copy_range(abuf, 0, host_len, host_out, (size_t)URL_HOST_MAX + 2);
    uc_copy_range(pbuf, 0, strlen(pbuf), path_out, (size_t)URL_PATH_MAX + 2);
    *port_out = port;
    *explicit_out = explicit_port;
    *out_reason = "ok";
    s.drop();
    return CPLUS_EXIT_OK;
}

static void url_case(const char *name, const char *value, int expect,
                     int *checks, int *valid, int *refused, int *failed) {
    char scheme[URL_SCHEME_MAX + 2];
    char host[URL_HOST_MAX + 2];
    char path[URL_PATH_MAX + 2];
    unsigned int port = 0u;
    int explicit_port = 0;
    const char *reason = "unknown";
    int code = uc_validate(value, scheme, host, &port, &explicit_port, path,
                           &reason);

    (*checks)++;
    if (code == CPLUS_EXIT_OK) {
        if (expect != CPLUS_EXIT_OK) {
            print("URL", name, "FAIL expected-refusal", expect);
            (*failed)++;
        } else {
            print("URL", name, "VALID", scheme, host, port, path,
                  explicit_port ? "explicit" : "default");
            (*valid)++;
        }
        return;
    }
    {
        error e = error();
        e.set((cplus_exit_code)code, reason);
        if (expect != 0 && expect != code) {
            print("URL", name, "FAIL wrong-code", code, expect, reason);
            (*failed)++;
        } else {
            print("URL", name, "REFUSED", code,
                  stdlib_category_name(e.category()), reason);
            (*refused)++;
        }
    }
}

static const char *url_case_name[] = {
    "v_http",        "v_https_port",  "v_ip_port",    "h_bad_scheme",
    "h_empty_auth",  "h_no_scheme",   "h_space_host", "h_big_port",
    "h_alpha_port",  "h_empty_port",  "h_bad_edge",   "h_traversal",
    "h_oversized"
};
static const char *url_case_value[] = {
    "http://example.com/",
    "https://example.com:8443/a/b",
    "http://127.0.0.1:8080",
    "ftp://example.com/",
    "http://",
    "not a url",
    "http://exa mple.com/",
    "http://example.com:99999/",
    "http://example.com:abc/",
    "http://example.com:/",
    "http://.example.com/",
    "http://example.com/../etc",
    "http://" "aaaaaaaaaa" "aaaaaaaaaa" "aaaaaaaaaa" "aaaaaaaaaa"
    "aaaaaaaaaa" "aaaaaaaaaa" "aaaaaaaaaa" "aaaaaaaaaa" "aaaaaaaaaa"
    "aaaaaaaaaa" "aaaaaaaaaa" "aaaaaaaaaa" "aaaaaaaaaa"
};
static int url_case_expect[] = {
    CPLUS_EXIT_OK, CPLUS_EXIT_OK, CPLUS_EXIT_OK, CPLUS_EXIT_UNSUPPORTED,
    CPLUS_EXIT_UNSUPPORTED, CPLUS_EXIT_UNSUPPORTED,
    CPLUS_EXIT_UNSUPPORTED, CPLUS_EXIT_BOUNDS,
    CPLUS_EXIT_UNSUPPORTED, CPLUS_EXIT_UNSUPPORTED,
    CPLUS_EXIT_UNSUPPORTED, CPLUS_EXIT_UNSUPPORTED,
    CPLUS_EXIT_BOUNDS
};
#define URL_CASE_COUNT (sizeof(url_case_name) / sizeof(url_case_name[0]))

int main(int argc, char **argv) {
    int checks = 0;
    int valid = 0;
    int refused = 0;
    int failed = 0;
    int i;

    stdlib_set_error_callback(NULL);
    stdlib_alloc_fail_reset();

    for (i = 0; i < (int)URL_CASE_COUNT; i++)
        url_case(url_case_name[i], url_case_value[i], url_case_expect[i],
                 &checks, &valid, &refused, &failed);
    for (i = 1; i < argc; i++) {
        const char *v = url_arg(argv, i);
        if (v == NULL)
            url_case("argv_null", "", CPLUS_EXIT_UNSUPPORTED,
                     &checks, &valid, &refused, &failed);
        else
            url_case("argv", v, CPLUS_EXIT_OK,
                     &checks, &valid, &refused, &failed);
    }

    print("URL_SUMMARY", checks, valid, refused, failed);
    return failed == 0 ? 0 : 1;
}
