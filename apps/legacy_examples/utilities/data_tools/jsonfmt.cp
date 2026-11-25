/* jsonfmt.cp -- deterministic, budgeted JSON pretty-printer.
 *
 * Build (from the repository root):
 *   ./build/c+ build examples/utilities/data_tools/jsonfmt.cp \
 *     --strict --sanitize=none -I. \
 *     -Ithird_party/yyjson -Lbuild/ext -lyyjson \
 *     -o examples/utilities/data_tools/.run/jsonfmt
 *
 * Usage:
 *   jsonfmt <json-text>
 *
 * Parsing goes through stdlib/json.hp::json_parse_budget with a depth cap of 8
 * and a local input-length cap of 2048 bytes. Malformed input, truncated
 * input, over-nested input and oversized input are ALL refused with a stable
 * token before any value is read. The parsed document is canonicalised by
 * json_view::write() (yyjson, NOFLAG) and then rendered as deterministic,
 * indented lines by a small local formatter. No clock, no files, no shell.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/json.hp"

#define JF_MAX_INPUT 2048
#define JF_BUF       2048
#define JF_LINE      256
#define JF_DEPTH     8

/* argv is an FFI pointer: copy the slot with memcpy so --strict can prove it. */
static char *jf_arg(char **p, size_t idx) {
    char *value;
    __builtin_memcpy(&value, p + idx, sizeof(value));
    return value;
}

static void jf_refuse(error *err, const char *code, cplus_exit_code status) {
    error_fail(err, status, code);
    print("JSON error", code);
}

/* Append a single byte to the current output line, emitting depth-based
 * indentation once at the start of a fresh line. Returns 0 on line overflow. */
static int jf_put(char *line, size_t cap, size_t *at, int *need_indent,
                  int depth, char ch) {
    size_t spaces;
    size_t k;
    if (*need_indent) {
        spaces = (size_t)depth * 2u;
        for (k = 0; k < spaces; k++) {
            if (*at + 1 >= cap)
                return 0;
            line[*at] = ' ';
            (*at)++;
        }
        *need_indent = 0;
    }
    if (*at + 1 >= cap)
        return 0;
    line[*at] = ch;
    (*at)++;
    line[*at] = '\0';
    return 1;
}

static int jf_flush(const char *line, size_t at, int *need_indent) {
    if (at == 0)
        return 1;
    print("JSON", line);
    *need_indent = 1;
    return 1;
}

/* Render the canonical compact JSON as indented lines. */
static int jf_pretty(const char *text, size_t len) {
    char line[JF_LINE];
    size_t at = 0;
    size_t i;
    int depth = 0;
    int need_indent = 1;
    int in_string = 0;
    int escaped = 0;
    char ch;

    line[0] = '\0';
    for (i = 0; i < len; i++) {
        __builtin_memcpy(&ch, text + i, 1);
        if (in_string) {
            if (!jf_put(line, sizeof(line), &at, &need_indent, depth, ch))
                return 0;
            if (escaped)
                escaped = 0;
            else if (ch == '\\')
                escaped = 1;
            else if (ch == '"')
                in_string = 0;
            continue;
        }
        if (ch == '"') {
            in_string = 1;
            if (!jf_put(line, sizeof(line), &at, &need_indent, depth, ch))
                return 0;
        } else if (ch == '{' || ch == '[') {
            if (!jf_put(line, sizeof(line), &at, &need_indent, depth, ch))
                return 0;
            if (!jf_flush(line, at, &need_indent))
                return 0;
            at = 0;
            line[0] = '\0';
            depth++;
        } else if (ch == '}' || ch == ']') {
            if (!jf_flush(line, at, &need_indent))
                return 0;
            at = 0;
            line[0] = '\0';
            if (depth > 0)
                depth--;
            if (!jf_put(line, sizeof(line), &at, &need_indent, depth, ch))
                return 0;
        } else if (ch == ',') {
            if (!jf_put(line, sizeof(line), &at, &need_indent, depth, ch))
                return 0;
            if (!jf_flush(line, at, &need_indent))
                return 0;
            at = 0;
            line[0] = '\0';
        } else if (ch == ':') {
            if (!jf_put(line, sizeof(line), &at, &need_indent, depth, ch))
                return 0;
            if (!jf_put(line, sizeof(line), &at, &need_indent, depth, ' '))
                return 0;
        } else if (ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r') {
            continue; /* canonical form has no insignificant whitespace */
        } else {
            if (!jf_put(line, sizeof(line), &at, &need_indent, depth, ch))
                return 0;
        }
    }
    if (!jf_flush(line, at, &need_indent))
        return 0;
    return 1;
}

int main(int argc, char **argv) {
    error err = error();
    const char *text;
    size_t len = 0;
    size_t out_len = 0;
    char buf[JF_BUF];
    char *canonical = NULL;
    json doc;
    json_view root;

    if (argc != 2) {
        jf_refuse(&err, "usage", CPLUS_EXIT_UNSUPPORTED);
        return 2;
    }
    text = jf_arg(argv, 1);
    if (text == NULL) {
        jf_refuse(&err, "usage", CPLUS_EXIT_UNSUPPORTED);
        return 2;
    }
    len = strlen(text);
    if (len == 0) {
        jf_refuse(&err, "empty", CPLUS_EXIT_UNSUPPORTED);
        return 1;
    }
    if (len > JF_MAX_INPUT) {
        jf_refuse(&err, "too-long", CPLUS_EXIT_BOUNDS);
        return 1;
    }

    doc = json_parse_budget(text, len, JF_DEPTH, &err);
    if (!doc.valid()) {
        jf_refuse(&err, err.code() == (int)CPLUS_EXIT_BOUNDS
                            ? "depth" : "parse",
                  CPLUS_EXIT_UNSUPPORTED);
        return 1;
    }
    root = doc.root();
    if (!root.valid()) {
        doc.drop();
        jf_refuse(&err, "parse", CPLUS_EXIT_UNSUPPORTED);
        return 1;
    }
    if (root.is_object())
        print("JSON root object");
    else if (root.is_array())
        print("JSON root array");
    else
        print("JSON root scalar");

    canonical = root.write(&out_len);
    if (canonical == NULL || out_len == 0 || out_len + 1 > sizeof(buf)) {
        if (canonical != NULL)
            json_free(canonical);
        doc.drop();
        jf_refuse(&err, "too-long", CPLUS_EXIT_BOUNDS);
        return 1;
    }
    memcpy(buf, canonical, out_len);
    buf[out_len] = '\0';
    json_free(canonical);

    if (!jf_pretty(buf, out_len)) {
        doc.drop();
        jf_refuse(&err, "line-too-long", CPLUS_EXIT_BOUNDS);
        return 1;
    }
    doc.drop();
    return 0;
}
