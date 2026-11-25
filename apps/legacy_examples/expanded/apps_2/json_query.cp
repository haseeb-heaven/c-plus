/* json_query.cp -- deterministic JSON field extraction with budget refusals.
 *
 * Build (from the repository root):
 *   ./build/c+ build apps/legacy_examples/expanded/apps_2/json_query.cp \
 *     --strict --sanitize=none -I. \
 *     -Ithird_party/yyjson -Lbuild/ext -lyyjson \
 *     -o apps/legacy_examples/expanded/apps_2/.run/json_query
 *
 * Usage:
 *   json_query <json-text> <dotted-path>
 *
 * The path is a dot-separated list of object keys and array indices, e.g.
 * "user.name", "items.0.tag". The document is parsed with the checked
 * `stdlib/json.hp::json_parse_budget` entry point with a depth cap of 8, so
 * over-nested and malformed/truncated input is refused before any value is
 * read. Every refusal records a typed `error *` and prints one stable token.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <limits.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/string.hp"
#include "stdlib/json.hp"

#define JQ_MAX_DEPTH    ((size_t)8)
#define JQ_PATH_CAP     128
#define JQ_SEG_CAP      64
#define JQ_MAX_SEGS     8

static char *jq_arg(char **p, size_t idx) {
    char *value;
    __builtin_memcpy(&value, p + idx, sizeof(value));
    return value;
}

static void jq_refuse(const char *reason) {
    print("JSON error", reason);
}

/* Extract segment #index from a local, NUL-terminated dotted path. */
static int jq_segment(const char *path, size_t index,
                      char *out, size_t cap) {
    size_t total = strlen(path);
    size_t start = 0;
    size_t current = 0;
    size_t i;
    size_t k;
    for (i = 0; i <= total; i++) {
        char ch = (i == total) ? '.' : path[i];
        if (ch == '.') {
            size_t n = i - start;
            if (current == index) {
                if (n == 0 || n + 1 > cap)
                    return 0;
                for (k = 0; k < n; k++)
                    out[k] = path[start + k];
                out[n] = '\0';
                return 1;
            }
            current++;
            start = i + 1;
        }
    }
    return 0;
}

static int jq_parse_index(const char *text, size_t *out) {
    char *end = (char *)text;
    long v;
    size_t i;
    size_t n = strlen(text);
    if (n == 0)
        return 0;
    for (i = 0; i < n; i++) {
        if (text[i] < '0' || text[i] > '9')
            return 0;
    }
    errno = 0;
    v = strtol(text, &end, 10);
    if (end == text || *end != '\0' || errno == ERANGE || v < 0)
        return 0;
    if (v > (long)INT_MAX)
        return 0;
    *out = (size_t)v;
    return 1;
}

static int jq_emit(json_view view, const char *path, error *err) {
    if (view.is_string()) {
        const char *value = NULL;
        if (view.get_string(&value, err) != STDLIB_OK || value == NULL) {
            jq_refuse("bad-value");
            return 1;
        }
        print("JSON value", path, value);
        return 0;
    }
    if (view.is_bool()) {
        int value = 0;
        if (view.get_bool(&value, err) != STDLIB_OK) {
            jq_refuse("bad-value");
            return 1;
        }
        print("JSON value", path, value ? "true" : "false");
        return 0;
    }
    if (view.is_number()) {
        int value = 0;
        if (view.get_int(&value, err) != STDLIB_OK) {
            jq_refuse("unsupported-type");
            return 1;
        }
        print("JSON value", path, value);
        return 0;
    }
    jq_refuse("unsupported-type");
    return 1;
}

int main(int argc, char **argv) {
    char path_buf[JQ_PATH_CAP];
    char seg[JQ_SEG_CAP];
    error err = error();
    const char *text;
    const char *path_arg;
    size_t path_len;
    size_t total;
    size_t k;
    json doc;
    json_view cur;
    string holder;

    if (argc != 3) {
        jq_refuse("usage");
        return 2;
    }
    text = jq_arg(argv, 1);
    path_arg = jq_arg(argv, 2);
    if (text == NULL || path_arg == NULL) {
        jq_refuse("usage");
        return 2;
    }

    holder = string(path_arg);
    if (!holder.valid()) {
        holder.drop();
        jq_refuse("bad-path");
        return 1;
    }
    path_len = holder.size();
    if (path_len == 0 || path_len + 1 > sizeof(path_buf)) {
        holder.drop();
        jq_refuse("bad-path");
        return 1;
    }
    if (holder.substr(0, path_len, path_buf, sizeof(path_buf)) != 0) {
        holder.drop();
        jq_refuse("bad-path");
        return 1;
    }
    holder.drop();

    total = 1;
    for (k = 0; k < path_len; k++) {
        if (path_buf[k] == '.')
            total++;
    }
    if (total > JQ_MAX_SEGS) {
        jq_refuse("path-too-deep");
        return 1;
    }

    doc = json_parse_budget(text, strlen(text), JQ_MAX_DEPTH, &err);
    if (!doc.valid()) {
        jq_refuse(err.code() == (int)CPLUS_EXIT_BOUNDS ? "depth" : "parse");
        return 1;
    }

    cur = doc.root();
    if (!cur.valid()) {
        doc.drop();
        jq_refuse("parse");
        return 1;
    }
    for (k = 0; k < total; k++) {
        size_t index;
        if (!jq_segment(path_buf, k, seg, sizeof(seg))) {
            doc.drop();
            jq_refuse("bad-path");
            return 1;
        }
        if (cur.is_object()) {
            cur = cur.member(seg);
        } else if (cur.is_array()) {
            if (!jq_parse_index(seg, &index) || index >= cur.size()) {
                doc.drop();
                jq_refuse("bad-index");
                return 1;
            }
            cur = cur.at(index);
        } else {
            doc.drop();
            jq_refuse("bad-path");
            return 1;
        }
        if (!cur.valid()) {
            doc.drop();
            jq_refuse("missing-key");
            return 1;
        }
    }

    k = jq_emit(cur, path_buf, &err);
    doc.drop();
    return (int)k;
}
