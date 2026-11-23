/* Native C+ replacement for tests/libc/validate_contracts.py.
 *
 * Validates machine-readable contract schemas and the bundled symbol
 * declarations without an interpreter.  Deliberately uses explicit checks
 * (never bare assert): every failure prints
 *   "contract validation FAILED: <message>"
 * on stderr and exits non-zero.  Success prints the same summary line as the
 * Python original:
 *   "contract validation: ok (N files validated, M C17 APIs)"
 *
 * JSON is parsed with stdlib/json.hp (yyjson).  Plain-text inputs (the public
 * header and the human-readable matrix) are read with stdlib/file.hp.
 *
 * Build (from the repository root, operands kept separate):
 *   ./build/c+ build tests/libc/validate_contracts.cp --strict --sanitize=none \
 *     -Ithird_party/yyjson -Lbuild/ext -lyyjson \
 *     -o build/native-tools/validate_contracts
 */
#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/file.hp"
#include "stdlib/json.hp"

#define TEXT_CAP (1u << 21) /* 2 MiB is ample for the header and the matrix */

static char header_text[TEXT_CAP];
static char matrix_text[TEXT_CAP];

static const char *CONTRACT_PATHS[5] = {
    "libc/contracts/contracts.json",
    "libc/contracts/ownership_contracts.json",
    "libc/contracts/libc_contracts.json",
    "libc/contracts/symbol_map.json",
    "libc/contracts/c17_contracts.json",
};

static const char *CLASSIFICATIONS[5] = {
    "IMPLEMENTED", "VENDORED", "HOST-DELEGATED", "PARTIAL", "UNSUPPORTED",
};

static const char *C17_CLASSIFICATIONS[6] = {
    "IMPLEMENTED_BUNDLED", "IMPLEMENTED_UPSTREAM", "PLATFORM_BACKED",
    "PARTIAL", "MISSING", "UNSUPPORTED",
};

/* ------------------------------------------------------------------ failure */

static void fail(const char *message) {
    fprintf(stderr, "contract validation FAILED: %s\n", message);
    exit(1);
}

static void failf(const char *fmt, ...) {
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    fail(buf);
}

/* ------------------------------------------------------------- small helpers */

static int is_word_char(int c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9') || c == '_';
}

static int is_ident_start(int c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

/* Characters accepted by the Python regex class [A-Za-z0-9_ \t*]. */
static int is_ret_class(int c) {
    return is_word_char(c) || c == ' ' || c == '\t' || c == '*';
}

/* Verify that obj is an object carrying every key in required[]. */
static void need_keys(json_view obj, const char **required, int count,
                      const char *where) {
    int k;
    if (!obj.is_object())
        failf("%s: expected object", where);
    for (k = 0; k < count; k++) {
        json_view found = obj.member(required[k]);
        if (!found.valid())
            failf("%s: missing required key '%s'", where, required[k]);
    }
}

static int is_nonempty_string(json_view v) {
    const char *s = NULL;
    error e = error();
    if (v.get_string(&s, &e) != STDLIB_OK || s == NULL)
        return 0;
    return s[0] != '\0';
}

/* Equivalent of isinstance(v, str) and bool(v.strip()). */
static int is_nonempty_trimmed(json_view v) {
    const char *s = NULL;
    size_t len;
    error e = error();
    if (v.get_string(&s, &e) != STDLIB_OK || s == NULL)
        return 0;
    while (*s != '\0' && isspace((unsigned char)*s))
        s++;
    len = strlen(s);
    while (len > 0 && isspace((unsigned char)s[len - 1]))
        len--;
    return len > 0;
}

static int is_nonempty_array(json_view v) {
    return v.is_array() && v.size() > 0;
}

static int is_nonempty_object(json_view v) {
    return v.is_object() && v.size() > 0;
}

/* pathlib.Path(...).is_file() equivalent for our repo-relative paths. */
static int path_is_file(const char *path) {
    file f = file(path, "r");
    if (!f.is_open())
        return 0;
    f.drop();
    return 1;
}

/* Read a whole text file via stdlib/file.hp::read_line. */
static int read_text(const char *path, char *buf, size_t cap, error *err) {
    file f = file(path, "r");
    size_t used = 0;
    if (!f.is_open())
        return 0;
    buf[0] = '\0';
    for (;;) {
        char line[2048];
        size_t n;
        int st = f.read_line(line, sizeof(line), err);
        if (st == STDLIB_EOF)
            break;
        if (st == STDLIB_FAIL) {
            f.drop();
            return 0;
        }
        n = strlen(line);
        if (used + n + 2 > cap) {
            f.drop();
            return 0;
        }
        memcpy(buf + used, line, n);
        used += n;
        buf[used++] = '\n';
        buf[used] = '\0';
    }
    f.drop();
    return 1;
}

/* ------------------------------------------- declared_as_function (regex eq) */

/* Replicates the Python validator's prototype regex:
 *   (?:^|[;{}])\s*[A-Za-z_][A-Za-z0-9_ \t*]*?\b<symbol>\s*
 *   \([^;{}]*\)\s*(?:CPLUS_[A-Z_]+\s*\([^)]*\))?\s*;
 * A plain "\bname\s*\(" search is not enough: it also hits macro definitions
 * and comments.  Requiring an isolated word, a parenthesised parameter list
 * free of ';' / '{' / '}', and a terminating ';' keeps those out. */
/* Match \( [^;{}]* \) \s* (CPLUS_[A-Z_]+\s*\([^)]*\))? \s* ; starting at the
 * '(' in text[r].  Returns 1 when a full prototype terminator is present. */
static int match_prototype_tail(const char *text, size_t n, size_t r) {
    size_t s, last_close, k, t;

    /* [^;{}]* : stop at the first ';', '{' or '}'. */
    s = r + 1;
    while (s < n && text[s] != ';' && text[s] != '{' && text[s] != '}')
        s++;
    last_close = (size_t)-1;
    for (k = r + 1; k < s; k++)
        if (text[k] == ')')
            last_close = k;
    if (last_close == (size_t)-1)
        return 0;

    /* \s* (CPLUS_[A-Z_]+\s*\([^)]*\))? \s* ; */
    t = last_close + 1;
    while (t < n && isspace((unsigned char)text[t]))
        t++;
    if (t + 6 <= n && strncmp(text + t, "CPLUS_", 6) == 0) {
        size_t u = t + 6;
        if (u < n && ((text[u] >= 'A' && text[u] <= 'Z') || text[u] == '_')) {
            while (u < n && ((text[u] >= 'A' && text[u] <= 'Z') || text[u] == '_'))
                u++;
            while (u < n && isspace((unsigned char)text[u]))
                u++;
            if (u < n && text[u] == '(') {
                u++;
                while (u < n && text[u] != ')')
                    u++;
                if (u < n && text[u] == ')') {
                    t = u + 1;
                    while (t < n && isspace((unsigned char)text[t]))
                        t++;
                }
            }
        }
    }
    return t < n && text[t] == ';';
}

static int declared_as_function(const char *text, const char *symbol) {
    size_t n = strlen(text);
    size_t slen = strlen(symbol);
    size_t i;
    if (slen == 0)
        return 0;

    for (i = 0; i < n; i++) {
        size_t p, q;
        int anchored = 0;

        if (i == 0 || text[i - 1] == '\n' || text[i - 1] == ';' ||
            text[i - 1] == '{' || text[i - 1] == '}')
            anchored = 1;
        if (!anchored)
            continue;

        /* \s* then [A-Za-z_] (the return type's first character). */
        p = i;
        while (p < n && isspace((unsigned char)text[p]))
            p++;
        if (p >= n || !is_ident_start((unsigned char)text[p]))
            continue;
        p++;

        /* [A-Za-z0-9_ \t*]*? up to a word-bounded occurrence of symbol. */
        q = p;
        while (q < n && is_ret_class((unsigned char)text[q])) {
            if (q > p && q + slen <= n &&
                !is_word_char((unsigned char)text[q - 1]) &&
                strncmp(text + q, symbol, slen) == 0) {
                size_t r = q + slen;
                while (r < n && isspace((unsigned char)text[r]))
                    r++;
                if (r < n && text[r] == '(' && match_prototype_tail(text, n, r))
                    return 1;
            }
            q++;
        }
    }
    return 0;
}

/* --------------------------------------------------------------- validators */

static void check_version(json_view root, int expected, const char *name) {
    error e = error();
    json_view v = root.member("version");
    int got = -1;
    if (!v.is_number() || v.get_int(&got, &e) != STDLIB_OK || got != expected)
        failf("%s: version must be %d (got %d)", name, expected, got);
}

static void validate_contracts_json(json_view root) {
    const char *req[6] = {"name", "header", "ownership", "bounds", "invalid",
                          "errors"};
    json_view apis = root.member("apis");
    size_t i, n;
    if (!apis.is_array() || apis.size() == 0)
        fail("contracts.json: apis must be a non-empty list");
    n = apis.size();
    for (i = 0; i < n; i++) {
        json_view api = apis.at(i);
        json_view name_view;
        need_keys(api, req, 6, "contracts.json api");
        name_view = api.member("name");
        if (!is_nonempty_string(name_view))
            fail("contracts.json: api name must be a non-empty string");
    }
}

static void validate_ownership(json_view root) {
    const char *req[4] = {"name", "params", "return", "thread_safety"};
    json_view functions = root.member("functions");
    size_t i, n;
    if (!functions.is_array() || functions.size() == 0)
        fail("ownership_contracts.json: functions must be a non-empty list");
    n = functions.size();
    for (i = 0; i < n; i++)
        need_keys(functions.at(i), req, 4, "ownership_contracts.json function");
}

static void validate_libc_contracts(json_view root) {
    const char *req[4] = {"params", "ownership", "failure", "returnState"};
    json_view functions = root.member("functions");
    yyjson_val *key, *val;
    size_t idx, max;
    if (!functions.is_object() || functions.size() == 0)
        fail("libc_contracts.json: functions must be a non-empty object");
    yyjson_obj_foreach(functions.value, idx, max, key, val) {
        json_view spec = json_view_of(val);
        char where[160];
        const char *kname = yyjson_get_str(key);
        snprintf(where, sizeof(where), "libc_contracts.json functions['%s']",
                 kname != NULL ? kname : "");
        need_keys(spec, req, 4, where);
    }
}

/* True when a JSON value is exactly null (json_view has no is_null helper). */
static int view_is_null(json_view v) {
    size_t len = 0;
    char *dump;
    int result;
    if (!v.valid())
        return 0;
    dump = v.write(&len);
    if (dump == NULL)
        return 0;
    result = (strcmp(dump, "null") == 0);
    json_free(dump);
    return result;
}

static void validate_symbol_map(json_view root) {
    const char *req[5] = {"standard", "bundled_symbol", "safe_symbol", "header",
                          "classification"};
    json_view mappings = root.member("mappings");
    size_t i, n;
    if (!mappings.is_array() || mappings.size() == 0)
        fail("symbol_map.json: mappings must be a non-empty list");
    n = mappings.size();
    for (i = 0; i < n; i++) {
        json_view entry = mappings.at(i);
        json_view sym;
        const char *cls = NULL;
        const char *symstr = NULL;
        char where[96];
        int k, valid = 0;
        error e = error();

        snprintf(where, sizeof(where), "symbol_map.json mappings[%zu]", i);
        need_keys(entry, req, 5, where);

        {
            json_view cls_view = entry.member("classification");
            if (cls_view.get_string(&cls, &e) == STDLIB_OK) {
                for (k = 0; k < 5; k++)
                    if (strcmp(cls, CLASSIFICATIONS[k]) == 0)
                        valid = 1;
            }
        }
        if (!valid)
            failf("%s: invalid classification '%s'", where,
                  cls != NULL ? cls : "(non-string)");

        sym = entry.member("bundled_symbol");
        if (!view_is_null(sym)) {
            if (sym.get_string(&symstr, &e) != STDLIB_OK || symstr == NULL ||
                !declared_as_function(header_text, symstr))
                failf("%s: bundled_symbol '%s' not declared as a function in "
                      "cplus_libc.h", where, symstr != NULL ? symstr : "(non-string)");
        }
    }
}

/* Validate c17_contracts.json; returns the number of declared C17 APIs. */
static int validate_c17(json_view data) {
    const char *req_top[2] = {"schema_version", "functions"};
    const char *req_fn[9] = {"name", "header", "classification", "source", "test",
                             "host_backend", "contracts", "logging",
                             "safety_limitations"};
    const char *req_log[3] = {"enabled_by", "records", "tests"};
    const char *str_fields[4] = {"header", "source", "test", "host_backend"};
    const char *path_fields[2] = {"source", "test"};
    json_view functions;
    size_t count, i, j;
    const char *seen[1024];
    int seen_count = 0;
    error e = error();

    need_keys(data, req_top, 2, "c17_contracts.json");
    {
        int version = -1;
        json_view v = data.member("schema_version");
        if (!v.is_number() || v.get_int(&version, &e) != STDLIB_OK || version != 1)
            fail("c17_contracts.json: schema_version must be 1");
    }

    functions = data.member("functions");
    if (!functions.is_array() || functions.size() == 0)
        fail("c17_contracts.json: functions must be a non-empty list");
    count = functions.size();
    if (count > 1024)
        fail("c17_contracts.json: too many functions");

    for (i = 0; i < count; i++) {
        json_view fn = functions.at(i);
        json_view logging;
        json_view name_view;
        json_view cls_view;
        json_view path_view;
        const char *name = NULL;
        const char *cls = NULL;
        const char *path = NULL;
        char where[96];
        int valid = 0, k;

        snprintf(where, sizeof(where), "c17_contracts.json functions[%zu]", i);
        need_keys(fn, req_fn, 9, where);

        name_view = fn.member("name");
        if (name_view.get_string(&name, &e) != STDLIB_OK || name == NULL ||
            strncmp(name, "cplus_", 6) != 0 || name[6] == '\0')
            failf("%s: name must be a non-empty cplus_* symbol", where);
        for (k = 0; k < seen_count; k++) {
            if (strcmp(seen[k], name) == 0)
                failf("%s: duplicate name '%s'", where, name);
        }
        seen[seen_count++] = name;

        cls_view = fn.member("classification");
        if (cls_view.get_string(&cls, &e) == STDLIB_OK) {
            for (k = 0; k < 6; k++)
                if (strcmp(cls, C17_CLASSIFICATIONS[k]) == 0)
                    valid = 1;
        }
        if (!valid)
            failf("%s: invalid classification '%s'", where,
                  cls != NULL ? cls : "(non-string)");

        for (j = 0; j < 4; j++) {
            if (!is_nonempty_trimmed(fn.member(str_fields[j])))
                failf("%s: '%s' must be a non-empty string", where, str_fields[j]);
        }
        for (j = 0; j < 2; j++) {
            path = NULL;
            path_view = fn.member(path_fields[j]);
            if (path_view.get_string(&path, &e) != STDLIB_OK ||
                path == NULL || !path_is_file(path))
                failf("%s: %s path does not exist: %s", where, path_fields[j],
                      path != NULL ? path : "(non-string)");
        }
        if (!is_nonempty_array(fn.member("contracts")))
            failf("%s: contracts must be a non-empty list", where);
        if (!is_nonempty_array(fn.member("safety_limitations")))
            failf("%s: safety_limitations must be a non-empty list", where);

        logging = fn.member("logging");
        {
            char lwhere[128];
            snprintf(lwhere, sizeof(lwhere), "%s.logging", where);
            need_keys(logging, req_log, 3, lwhere);
            if (!is_nonempty_array(logging.member("records")))
                failf("%s: records must be a non-empty list", lwhere);
            if (!is_nonempty_array(logging.member("tests")))
                failf("%s: tests must be a non-empty list", lwhere);
        }
        if (!declared_as_function(header_text, name))
            failf("%s: %s not declared as a function in cplus_libc.h", where, name);
    }

    /* Matrix row count must agree with the machine-readable contract. */
    if (path_is_file("docs/libc/API_TEST_MATRIX.md")) {
        int rows = 0;
        size_t p;
        if (!read_text("docs/libc/API_TEST_MATRIX.md", matrix_text, TEXT_CAP, &e))
            fail("c17_contracts.json: cannot read docs/libc/API_TEST_MATRIX.md");
        for (p = 0; matrix_text[p] != '\0'; p++) {
            if ((p == 0 || matrix_text[p - 1] == '\n') &&
                strncmp(matrix_text + p, "| cplus_", 8) == 0)
                rows++;
        }
        if ((size_t)rows != count)
            failf("c17_contracts.json: %zu functions but API_TEST_MATRIX.md has "
                  "%d rows", count, rows);
    }
    return (int)count;
}

/* ---------------------------------------------------------------------- main */

int main(void) {
    error err = error();
    int c17_count;
    size_t i;

    /* Load every required contract file first (missing / invalid JSON fails). */
    for (i = 0; i < 5; i++) {
        if (!path_is_file(CONTRACT_PATHS[i]))
            failf("missing contract file: %s", CONTRACT_PATHS[i]);
    }
    json d0 = json_parse_file(CONTRACT_PATHS[0], &err);
    if (!d0.valid())
        failf("invalid JSON in %s: %s", CONTRACT_PATHS[0], err.what());
    json d1 = json_parse_file(CONTRACT_PATHS[1], &err);
    if (!d1.valid())
        failf("invalid JSON in %s: %s", CONTRACT_PATHS[1], err.what());
    json d2 = json_parse_file(CONTRACT_PATHS[2], &err);
    if (!d2.valid())
        failf("invalid JSON in %s: %s", CONTRACT_PATHS[2], err.what());
    json d3 = json_parse_file(CONTRACT_PATHS[3], &err);
    if (!d3.valid())
        failf("invalid JSON in %s: %s", CONTRACT_PATHS[3], err.what());
    json d4 = json_parse_file(CONTRACT_PATHS[4], &err);
    if (!d4.valid())
        failf("invalid JSON in %s: %s", CONTRACT_PATHS[4], err.what());

    /* Read the public header once, before any symbol-declaration checks. */
    header_text[0] = '\0';
    if (!read_text("libc/install-include/cplus_libc.h", header_text, TEXT_CAP, &err))
        fail("cannot read libc/install-include/cplus_libc.h");

    check_version(d0.root(), 1, "contracts.json");
    check_version(d1.root(), 1, "ownership_contracts.json");
    check_version(d2.root(), 2, "libc_contracts.json");
    check_version(d3.root(), 1, "symbol_map.json");

    validate_contracts_json(d0.root());
    validate_ownership(d1.root());
    validate_libc_contracts(d2.root());
    validate_symbol_map(d3.root());
    c17_count = validate_c17(d4.root());

    printf("contract validation: ok (%d files validated, %d C17 APIs)\n", 5,
           c17_count);
    return 0;
}
