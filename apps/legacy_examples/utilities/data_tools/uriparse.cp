/* uriparse.cp -- bounded URL parser with strict refusals.
 *
 * Build (from the repository root):
 *   ./build/c+ build examples/utilities/data_tools/uriparse.cp \
 *     --strict --sanitize=none -I. \
 *     -o examples/utilities/data_tools/.run/uriparse
 *
 * Usage:
 *   uriparse <url>
 *
 * Accepts exactly: scheme://host[:port][/path]
 *   scheme: [A-Za-z][A-Za-z0-9+.-]*  (1..16 bytes)
 *   host:   [A-Za-z0-9.-]+            (1..128 bytes)
 *   port:   1..65535                  (decimal, <= 5 digits)
 *   path:   starts with '/', <= 256 bytes
 * Query strings, fragments, userinfo, whitespace, IPv6 literals and URLs
 * over 512 bytes are refused with stable tokens. Output is deterministic:
 * fixed argv, no clock, no files, no shell. A path of "-" means "absent".
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/string.hp"

#define UP_URL     512
#define UP_SCHEME   16
#define UP_HOST    128
#define UP_PATH    256

static char *up_arg(char **p, size_t idx) {
    char *value;
    __builtin_memcpy(&value, p + idx, sizeof(value));
    return value;
}

static void up_refuse(error *err, const char *code, cplus_exit_code status) {
    error_fail(err, status, code);
    print("URI error", code);
}

static int up_is_alpha(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

static int up_is_digit(char c) {
    return c >= '0' && c <= '9';
}

/* Copy an argv C string into a bounded local buffer via the checked string. */
static int up_copy(const char *src, char *dst, size_t cap, size_t *out_len) {
    string s = string(src);
    size_t n;
    int ok = 0;
    if (s.valid()) {
        n = s.size();
        if (n > 0 && n + 1 <= cap) {
            if (s.substr(0, n, dst, cap) == 0) {
                *out_len = n;
                ok = 1;
            }
        }
    }
    s.drop();
    return ok;
}

/* Copy url[start..end) into dst; returns 0 if empty or over cap. */
static int up_range(const char *url, size_t start, size_t end,
                    char *dst, size_t cap) {
    size_t n;
    size_t i;
    if (end <= start)
        return 0;
    n = end - start;
    if (n == 0 || n + 1 > cap)
        return 0;
    for (i = 0; i < n; i++)
        dst[i] = url[start + i];
    dst[n] = '\0';
    return 1;
}

static int up_parse_port(const char *text, int *out) {
    char *end = (char *)text;
    long v;
    size_t n = strlen(text);
    size_t i;
    if (n == 0 || n > 5)
        return 0;
    for (i = 0; i < n; i++) {
        if (!up_is_digit(text[i]))
            return 0;
    }
    errno = 0;
    v = strtol(text, &end, 10);
    if (end == text || *end != '\0' || errno == ERANGE || v < 1 || v > 65535)
        return 0;
    *out = (int)v;
    return 1;
}

int main(int argc, char **argv) {
    error err = error();
    char url[UP_URL];
    char scheme[UP_SCHEME + 1];
    char host[UP_HOST + 1];
    char portbuf[8];
    char path[UP_PATH + 1];
    size_t url_len = 0;
    size_t i;
    size_t colon = 0;
    size_t auth_start;
    size_t auth_end;
    size_t slash = 0;
    size_t host_colon = 0;
    int port = -1;
    int found_colon = 0;
    int found_scheme = 0;

    if (argc != 2) {
        up_refuse(&err, "usage", CPLUS_EXIT_UNSUPPORTED);
        return 2;
    }
    if (!up_copy(up_arg(argv, 1), url, sizeof(url), &url_len)) {
        up_refuse(&err, "too-long", CPLUS_EXIT_BOUNDS);
        return 1;
    }

    /* Reject features this strict parser does not model. */
    for (i = 0; i < url_len; i++) {
        char ch = url[i];
        if (ch == '?' || ch == '#') {
            up_refuse(&err, "unsupported", CPLUS_EXIT_UNSUPPORTED);
            return 1;
        }
        if (ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r' || ch == '@' ||
            ch == '[' || ch == ']') {
            up_refuse(&err, "bad-url", CPLUS_EXIT_UNSUPPORTED);
            return 1;
        }
    }

    /* Locate the "://" separator. */
    for (i = 0; i + 2 < url_len; i++) {
        if (url[i] == ':') {
            colon = i;
            found_colon = 1;
            break;
        }
    }
    if (!found_colon || url[colon + 1] != '/' || url[colon + 2] != '/') {
        up_refuse(&err, "bad-scheme", CPLUS_EXIT_UNSUPPORTED);
        return 1;
    }
    if (colon == 0 || colon > UP_SCHEME || !up_is_alpha(url[0])) {
        up_refuse(&err, "bad-scheme", CPLUS_EXIT_UNSUPPORTED);
        return 1;
    }
    for (i = 1; i < colon; i++) {
        char ch = url[i];
        if (!up_is_alpha(ch) && !up_is_digit(ch) && ch != '+' &&
            ch != '-' && ch != '.') {
            up_refuse(&err, "bad-scheme", CPLUS_EXIT_UNSUPPORTED);
            return 1;
        }
    }
    if (!up_range(url, 0, colon, scheme, sizeof(scheme))) {
        up_refuse(&err, "bad-scheme", CPLUS_EXIT_UNSUPPORTED);
        return 1;
    }
    found_scheme = 1;

    auth_start = colon + 3;
    slash = auth_start;
    while (slash < url_len && url[slash] != '/')
        slash++;
    auth_end = slash;

    /* Split authority into host[:port] at the LAST ':' (only one allowed). */
    for (i = auth_start; i < auth_end; i++) {
        if (url[i] == ':') {
            if (host_colon != 0) {
                up_refuse(&err, "bad-host", CPLUS_EXIT_UNSUPPORTED);
                return 1;
            }
            host_colon = i;
        }
    }
    if (host_colon != 0) {
        if (!up_range(url, auth_start, host_colon, host, sizeof(host)) ||
            host_colon + 1 >= auth_end ||
            !up_range(url, host_colon + 1, auth_end, portbuf,
                      sizeof(portbuf)) ||
            !up_parse_port(portbuf, &port)) {
            up_refuse(&err, "bad-port", CPLUS_EXIT_UNSUPPORTED);
            return 1;
        }
    } else {
        if (!up_range(url, auth_start, auth_end, host, sizeof(host))) {
            up_refuse(&err, "bad-host", CPLUS_EXIT_UNSUPPORTED);
            return 1;
        }
    }
    if (host[0] == '\0') {
        up_refuse(&err, "bad-host", CPLUS_EXIT_UNSUPPORTED);
        return 1;
    }
    for (i = 0; host[i] != '\0'; i++) {
        char ch = host[i];
        if (!up_is_alpha(ch) && !up_is_digit(ch) && ch != '.' && ch != '-') {
            up_refuse(&err, "bad-host", CPLUS_EXIT_UNSUPPORTED);
            return 1;
        }
    }

    if (!found_scheme)
        return 1;

    if (slash < url_len) {
        if (url[slash] != '/' || url_len - slash > UP_PATH) {
            up_refuse(&err, "bad-path", CPLUS_EXIT_BOUNDS);
            return 1;
        }
        if (!up_range(url, slash, url_len, path, sizeof(path))) {
            up_refuse(&err, "bad-path", CPLUS_EXIT_BOUNDS);
            return 1;
        }
    } else {
        path[0] = '-';
        path[1] = '\0';
    }

    print("URI scheme", scheme);
    print("URI host", host);
    print("URI port", port);
    print("URI path", path);
    return 0;
}
