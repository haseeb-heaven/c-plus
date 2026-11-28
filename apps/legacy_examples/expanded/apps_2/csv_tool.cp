/* csv_tool.cp -- deterministic CSV column aggregate with refusal proofs.
 *
 * Build (from the repository root):
 *   ./build/c+ build apps/legacy_examples/expanded/apps_2/csv_tool.cp \
 *     --strict --sanitize=none -I. \
 *     -o apps/legacy_examples/expanded/apps_2/.run/csv_tool
 *
 * Usage:
 *   csv_tool <path> <column> <sum|avg>
 *
 * The fixture is a fixed file with one integer row per line. Field extraction
 * uses the checked `stdlib/string.hp::field` API and every file read goes
 * through `stdlib/file.hp` with an `error *`. Row width, column index, numeric
 * syntax, row count and signed-sum overflow are all refused with a stable
 * token and a non-zero exit code. Averages use integer division guarded by an
 * explicit non-zero check. No clock, no prompts, no shell.
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

#define CSV_LINE_CAP  256
#define CSV_FIELD_CAP 40
#define CSV_MAX_COLS  64
#define CSV_MAX_ROWS  100000

typedef struct {
    long rows;
    size_t cols;
    long long sum;
} csv_result;

static char *csv_arg(char **p, size_t idx) {
    char *value;
    __builtin_memcpy(&value, p + idx, sizeof(value));
    return value;
}

static void csv_refuse(const char *reason) {
    print("CSV error", reason);
}

static int csv_parse_int(const char *text, long *out) {
    char *end = (char *)text;
    long v;
    if (text == NULL || *text == '\0')
        return 0;
    errno = 0;
    v = strtol(text, &end, 10);
    if (end == text || *end != '\0')
        return 0;
    if (errno == ERANGE)
        return 0;
    *out = v;
    return 1;
}

/* Exact length of field `index` inside a local string (no truncation). */
static int csv_field_len(string *rec, size_t index, size_t *out) {
    size_t n = rec->size();
    size_t current = 0;
    size_t start = 0;
    size_t i;
    for (i = 0; i <= n; i++) {
        char ch = (i == n) ? ',' : rec->at(i);
        if (ch == ',') {
            if (current == index) {
                *out = i - start;
                return 1;
            }
            current++;
            start = i + 1;
        }
    }
    return 0;
}

static int csv_value(const char *line, int col, long *out) {
    char field[CSV_FIELD_CAP];
    string rec = string(line);
    size_t len = 0;
    long value = 0;
    int ok = 0;

    if (!rec.valid()) {
        rec.drop();
        return 0;
    }
    if (!csv_field_len(&rec, (size_t)col, &len) ||
        len == 0 || len + 1 > sizeof(field)) {
        rec.drop();
        return 0;
    }
    if (rec.field(',', (size_t)col, field, sizeof(field)) != 0) {
        rec.drop();
        return 0;
    }
    rec.drop();
    if (!csv_parse_int(field, &value))
        return 0;
    *out = value;
    ok = 1;
    return ok;
}

/* Returns 0 on success, 1 on refusal with *reason set. */
static int csv_read(file *stream, int col, char *line, size_t line_cap,
                    error *err, csv_result *res, const char **reason) {
    long rows = 0;
    size_t cols = 0;
    long long sum = 0;
    int rr;

    res->rows = 0;
    res->cols = 0;
    res->sum = 0;
    for (;;) {
        string rec;
        size_t row_cols;
        long value = 0;

        if (rows >= CSV_MAX_ROWS) {
            *reason = "too-many-rows";
            return 1;
        }
        rr = stream->read_line(line, line_cap, err);
        if (rr == STDLIB_EOF)
            break;
        if (rr == STDLIB_FAIL) {
            *reason = err->code() == (int)CPLUS_EXIT_BOUNDS
                          ? "line-too-long" : "io-error";
            return 1;
        }

        rec = string(line);
        if (!rec.valid()) {
            rec.drop();
            *reason = "malformed-row";
            return 1;
        }
        row_cols = rec.count_fields(',');
        rec.drop();
        if (row_cols < 1 || row_cols > CSV_MAX_COLS) {
            *reason = "malformed-row";
            return 1;
        }
        if (rows == 0) {
            cols = row_cols;
            if ((size_t)col >= cols) {
                *reason = "column-out-of-range";
                return 1;
            }
        } else if (row_cols != cols) {
            *reason = "malformed-row";
            return 1;
        }

        if (!csv_value(line, col, &value)) {
            *reason = "not-numeric";
            return 1;
        }
        if ((value > 0 && sum > LLONG_MAX - (long long)value) ||
            (value < 0 && sum < LLONG_MIN - (long long)value)) {
            error_fail(err, CPLUS_EXIT_BOUNDS, "overflow");
            *reason = "overflow";
            return 1;
        }
        sum += (long long)value;
        rows++;
    }

    if (rows == 0 || cols == 0) {
        *reason = "empty-input";
        return 1;
    }
    res->rows = rows;
    res->cols = cols;
    res->sum = sum;
    return 0;
}

int main(int argc, char **argv) {
    error err = error();
    char line[CSV_LINE_CAP];
    csv_result res;
    const char *path;
    const char *mode;
    const char *col_text;
    const char *reason = "bad-input";
    long col_long = 0;
    int col;
    int is_avg;
    int status;

    if (argc != 4) {
        csv_refuse("usage");
        return 2;
    }
    path = csv_arg(argv, 1);
    col_text = csv_arg(argv, 2);
    mode = csv_arg(argv, 3);
    if (path == NULL || col_text == NULL || mode == NULL) {
        csv_refuse("usage");
        return 2;
    }
    if (!csv_parse_int(col_text, &col_long) || col_long < 0 ||
        col_long >= CSV_MAX_COLS) {
        csv_refuse("column-out-of-range");
        return 1;
    }
    col = (int)col_long;
    if (strcmp(mode, "sum") == 0) {
        is_avg = 0;
    } else if (strcmp(mode, "avg") == 0) {
        is_avg = 1;
    } else {
        csv_refuse("bad-mode");
        return 1;
    }

    {
        file stream = file(path, "r");
        if (!stream.is_open()) {
            error_fail(&err, CPLUS_EXIT_IO, "cannot-open");
            csv_refuse(err.what());
            stream.drop();
            return 1;
        }
        status = csv_read(&stream, col, line, sizeof(line), &err,
                          &res, &reason);
        stream.drop();
    }
    if (status != 0) {
        csv_refuse(reason);
        return 1;
    }
    if (is_avg) {
        long long denom = (long long)res.rows;
        long long avg;
        if (denom == 0) {
            csv_refuse("empty-input");
            return 1;
        }
        avg = res.sum / denom;
        print("CSV avg", col, avg);
    } else {
        print("CSV sum", col, res.sum);
    }
    print("CSV rows", res.rows);
    return 0;
}
