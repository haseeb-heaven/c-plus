/* csvstat.cp -- CSV statistics with overflow-safe arithmetic and refusals.
 *
 * Build (from the repository root):
 *   ./build/c+ build examples/utilities/data_tools/csvstat.cp \
 *     --strict --sanitize=none -I. \
 *     -o examples/utilities/data_tools/.run/csvstat
 *
 * Usage:
 *   csvstat <csv-path> stats|count|sum|min|max
 *
 * The fixture is a two-column CSV with the fixed header `name,value`. Each
 * data row is `name,<signed 64-bit integer>`. Every read goes through the
 * checked file::read_line API with an error* result; an over-long line, a
 * malformed row, a non-numeric cell, a missing header, too many rows or a
 * summation that would overflow are ALL refused with a stable token. Sum
 * accumulation is overflow-safe: the addition is guarded before it is
 * executed (no signed-overflow UB, no reliance on a sanitizer). Inputs and
 * output are fully deterministic: fixed argv, fixed file bytes, no clock.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <limits.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/string.hp"
#include "stdlib/file.hp"

#define CS_LINE   256   /* max bytes per line (incl. NUL) */
#define CS_PATH   512   /* max path bytes */
#define CS_CMD     32   /* max command bytes */
#define CS_ROWS    64   /* max data rows */
#define CS_NAME    64   /* max name bytes */

/* argv is an FFI pointer: copy the slot with memcpy so --strict can prove it. */
static char *cs_arg(char **p, size_t idx) {
    char *value;
    __builtin_memcpy(&value, p + idx, sizeof(value));
    return value;
}

static void cs_refuse(error *err, const char *code, cplus_exit_code status) {
    error_fail(err, status, code);
    print("CSV error", code);
}

/* Bounded copy of an argv C string into a local, caller-owned buffer. */
static int cs_copy(const char *src, char *dst, size_t cap, size_t *out_len) {
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

/* Strict signed-integer parse: optional leading sign, then digits only, no
 * trailing bytes, no ERANGE. The text is a NUL-terminated local buffer. */
static int cs_parse_int(const char *text, long long *out) {
    char *end = (char *)text;
    long long v;
    size_t n = strlen(text);
    size_t i = 0;
    if (n == 0)
        return 0;
    if (text[0] == '+' || text[0] == '-')
        i = 1;
    if (i >= n)
        return 0;
    for (; i < n; i++) {
        if (text[i] < '0' || text[i] > '9')
            return 0;
    }
    errno = 0;
    v = strtoll(text, &end, 10);
    if (end == text || *end != '\0' || errno == ERANGE)
        return 0;
    *out = v;
    return 1;
}

/* Overflow-safe running sum. Returns 0 on overflow (before any mutation). */
static int cs_add_checked(long long acc, long long cell, long long *out) {
    if (acc >= 0 && cell > LLONG_MAX - acc)
        return 0;
    if (acc < 0 && cell < LLONG_MIN - acc)
        return 0;
    *out = acc + cell;
    return 1;
}

int main(int argc, char **argv) {
    error err = error();
    file stream;
    char path[CS_PATH];
    char cmd[CS_CMD];
    char line[CS_LINE];
    char value[CS_LINE];
    size_t path_len = 0;
    size_t cmd_len = 0;
    size_t n;
    size_t i;
    size_t comma;
    const char *cmdp;
    long long sum = 0;
    long long mn = 0;
    long long mx = 0;
    long long cell = 0;
    int count = 0;
    int is_stats;
    int is_sum;
    int is_min;
    int is_max;
    int is_count;
    int rc;

    if (argc != 3) {
        cs_refuse(&err, "usage", CPLUS_EXIT_UNSUPPORTED);
        return 2;
    }
    if (!cs_copy(cs_arg(argv, 1), path, sizeof(path), &path_len) ||
        !cs_copy(cs_arg(argv, 2), cmd, sizeof(cmd), &cmd_len)) {
        cs_refuse(&err, "bad-arg", CPLUS_EXIT_BOUNDS);
        return 1;
    }

    is_stats = strcmp(cmd, "stats") == 0;
    is_sum   = strcmp(cmd, "sum") == 0;
    is_min   = strcmp(cmd, "min") == 0;
    is_max   = strcmp(cmd, "max") == 0;
    is_count = strcmp(cmd, "count") == 0;
    if (!is_stats && !is_sum && !is_min && !is_max && !is_count) {
        cs_refuse(&err, "bad-command", CPLUS_EXIT_UNSUPPORTED);
        return 1;
    }

    stream = file(path, "r");
    if (!stream.is_open()) {
        cs_refuse(&err, "cannot-open", CPLUS_EXIT_IO);
        return 1;
    }

    /* Header must be exactly `name,value`. */
    rc = stream.read_line(line, sizeof(line), &err);
    if (rc == STDLIB_EOF) {
        stream.drop();
        cs_refuse(&err, "empty", CPLUS_EXIT_UNSUPPORTED);
        return 1;
    }
    if (rc != STDLIB_OK) {
        const char *why = err.code() == (int)CPLUS_EXIT_BOUNDS
                              ? "line-too-long" : "read-error";
        stream.drop();
        cs_refuse(&err, why, CPLUS_EXIT_BOUNDS);
        return 1;
    }
    if (strcmp(line, "name,value") != 0) {
        stream.drop();
        cs_refuse(&err, "bad-header", CPLUS_EXIT_UNSUPPORTED);
        return 1;
    }

    for (;;) {
        rc = stream.read_line(line, sizeof(line), &err);
        if (rc == STDLIB_EOF)
            break;
        if (rc != STDLIB_OK) {
            const char *why = err.code() == (int)CPLUS_EXIT_BOUNDS
                                  ? "line-too-long" : "read-error";
            stream.drop();
            cs_refuse(&err, why, CPLUS_EXIT_BOUNDS);
            return 1;
        }
        n = strlen(line);
        if (n == 0) {
            stream.drop();
            cs_refuse(&err, "bad-row", CPLUS_EXIT_UNSUPPORTED);
            return 1;
        }

        /* Exactly one comma, a non-empty bounded name, then the value. */
        comma = 0;
        for (i = 0; i < n; i++) {
            if (line[i] == ',')
                comma++;
        }
        if (comma != 1) {
            stream.drop();
            cs_refuse(&err, "bad-row", CPLUS_EXIT_UNSUPPORTED);
            return 1;
        }
        comma = 0;
        for (i = 0; i < n; i++) {
            if (line[i] == ',') {
                comma = i;
                break;
            }
        }
        if (comma == 0 || comma > CS_NAME - 1 || comma + 1 >= n) {
            stream.drop();
            cs_refuse(&err, "bad-row", CPLUS_EXIT_UNSUPPORTED);
            return 1;
        }
        n = 0;
        for (i = comma + 1; i < strlen(line); i++) {
            if (n + 1 >= sizeof(value)) {
                stream.drop();
                cs_refuse(&err, "bad-row", CPLUS_EXIT_UNSUPPORTED);
                return 1;
            }
            value[n] = line[i];
            n++;
        }
        value[n] = '\0';
        if (!cs_parse_int(value, &cell)) {
            stream.drop();
            cs_refuse(&err, "non-numeric", CPLUS_EXIT_UNSUPPORTED);
            return 1;
        }

        if (!cs_add_checked(sum, cell, &sum)) {
            stream.drop();
            cs_refuse(&err, "sum-overflow", CPLUS_EXIT_BOUNDS);
            return 1;
        }
        if (count == 0) {
            mn = cell;
            mx = cell;
        } else {
            if (cell < mn)
                mn = cell;
            if (cell > mx)
                mx = cell;
        }
        count++;
        if (count > CS_ROWS) {
            stream.drop();
            cs_refuse(&err, "too-many-rows", CPLUS_EXIT_BOUNDS);
            return 1;
        }
    }

    if (stream.close(&err) != STDLIB_OK) {
        stream.drop();
        cs_refuse(&err, "close-error", CPLUS_EXIT_IO);
        return 1;
    }
    stream.drop();

    if (count == 0) {
        cs_refuse(&err, "no-data", CPLUS_EXIT_UNSUPPORTED);
        return 1;
    }

    if (is_stats) {
        print("CSV count", count);
        print("CSV sum", sum);
        print("CSV min", mn);
        print("CSV max", mx);
    } else if (is_sum) {
        print("CSV sum", sum);
    } else if (is_min) {
        print("CSV min", mn);
    } else if (is_max) {
        print("CSV max", mx);
    } else {
        print("CSV count", count);
    }
    return 0;
}
