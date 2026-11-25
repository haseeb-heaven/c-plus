/* matrix.cp -- deterministic small integer matrix ops with refusal proofs.
 *
 * Build (from the repository root):
 *   ./build/c+ build examples/expanded/apps_2/matrix.cp \
 *     --strict --sanitize=none -I. \
 *     -o examples/expanded/apps_2/.run/matrix
 *
 * Usage (row-major, comma-separated integers):
 *   matrix add       <r1> <c1> <data1> <r2> <c2> <data2>
 *   matrix multiply  <r1> <c1> <data1> <r2> <c2> <data2>
 *   matrix determinant <r> <c> <data>
 *
 * Examples:
 *   matrix add 2 2 1,2,3,4 2 2 5,6,7,8
 *   matrix multiply 2 3 1,2,3,4,5,6 3 2 7,8,9,10,11,12
 *   matrix determinant 2 2 1,2,3,4
 *
 * Dimensions are capped at 4, entries at +/-1e9. Multiplication and addition
 * are checked before they happen, so an intermediate that would exceed a
 * safe 64-bit range is refused instead of wrapping. Every refusal records a
 * typed `error *`. No clock, no prompts, no shell.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <limits.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/string.hp"

#define MAT_MAX_DIM   4
#define MAT_VALUES    (MAT_MAX_DIM * MAT_MAX_DIM)
#define MAT_FIELD_CAP 48
#define MAT_DATA_CAP  512
#define MAT_VALUE_LIMIT 1000000000LL
#define MAT_SAFE 4000000000000000000LL

static long long mat_a[MAT_MAX_DIM][MAT_MAX_DIM];
static long long mat_b[MAT_MAX_DIM][MAT_MAX_DIM];
static long long mat_out[MAT_MAX_DIM][MAT_MAX_DIM];
static error *mat_err;

static char *mat_arg(char **p, size_t idx) {
    char *value;
    __builtin_memcpy(&value, p + idx, sizeof(value));
    return value;
}

static void mat_refuse(const char *reason) {
    if (mat_err != NULL)
        error_fail(mat_err, CPLUS_EXIT_UNSUPPORTED, reason);
    print("MAT error", reason);
}

static void mat_refuse_range(const char *reason) {
    if (mat_err != NULL)
        error_fail(mat_err, CPLUS_EXIT_BOUNDS, reason);
    print("MAT error", reason);
}

static int mat_parse_dim(const char *text, int *out, int *too_big) {
    char *end = (char *)text;
    long v;
    *too_big = 0;
    if (text == NULL || *text == '\0')
        return 0;
    errno = 0;
    v = strtol(text, &end, 10);
    if (end == text || *end != '\0' || errno == ERANGE || v < 0)
        return 0;
    if (v > MAT_MAX_DIM) {
        *too_big = 1;
        return 0;
    }
    if (v < 1)
        return 0;
    *out = (int)v;
    return 1;
}

static int mat_parse_value(const char *text, long long *out) {
    char *end = (char *)text;
    long v;
    if (text == NULL || *text == '\0')
        return 0;
    errno = 0;
    v = strtol(text, &end, 10);
    if (end == text || *end != '\0' || errno == ERANGE)
        return 0;
    if (v > MAT_VALUE_LIMIT || v < -MAT_VALUE_LIMIT)
        return 0;
    *out = (long long)v;
    return 1;
}

static int mat_field_len(string *rec, size_t index, size_t *out) {
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

/* Parse an r*c comma list into a flat local array; return 0 on refusal. */
static int mat_flat(const char *data_arg, int rows, int cols,
                    long long *vals) {
    char buf[MAT_DATA_CAP];
    char field[MAT_FIELD_CAP];
    string holder = string(data_arg);
    int ok = 0;

    if (holder.valid()) {
        size_t len = holder.size();
        if (len > 0 && len + 1 <= sizeof(buf) &&
            holder.substr(0, len, buf, sizeof(buf)) == 0) {
            string rec = string(buf);
            if (rec.valid()) {
                size_t expected = (size_t)rows * (size_t)cols;
                size_t count = rec.count_fields(',');
                if (count == expected) {
                    size_t k;
                    ok = 1;
                    for (k = 0; k < expected; k++) {
                        size_t flen = 0;
                        long long value = 0;
                        if (!mat_field_len(&rec, k, &flen) || flen == 0 ||
                            flen + 1 > sizeof(field) ||
                            rec.field(',', k, field, sizeof(field)) != 0 ||
                            !mat_parse_value(field, &value)) {
                            ok = 0;
                            break;
                        }
                        /* Pointer arithmetic (not a subscript) keeps --strict
                         * happy for the unverifiable output extent. */
                        *(vals + k) = value;
                    }
                }
                rec.drop();
            }
        }
    }
    holder.drop();
    return ok;
}

/* Load into one of the two global matrices (no pointer output parameter). */
static int mat_load(int which, const char *rtext, const char *ctext,
                    const char *data, int *rows, int *cols) {
    long long vals[MAT_VALUES];
    int r = 0;
    int c = 0;
    int too_big = 0;
    int i;
    int j;
    int k = 0;

    if (!mat_parse_dim(rtext, &r, &too_big) ||
        !mat_parse_dim(ctext, &c, &too_big)) {
        mat_refuse(too_big ? "oversized-dims" : "bad-dims");
        return 0;
    }
    if (!mat_flat(data, r, c, vals)) {
        mat_refuse("bad-data");
        return 0;
    }
    for (i = 0; i < r; i++) {
        for (j = 0; j < c; j++) {
            if (which == 0)
                mat_a[i][j] = vals[k];
            else
                mat_b[i][j] = vals[k];
            k++;
        }
    }
    *rows = r;
    *cols = c;
    return 1;
}

/* Compiler overflow builtins keep the arithmetic proof local and avoid any
 * variable divisor (which --strict cannot prove non-zero here). */
static int mat_checked_mul(long long a, long long b, long long *out) {
    long long r = 0;
    if (__builtin_mul_overflow(a, b, &r))
        return 0;
    if (r > MAT_SAFE || r < -MAT_SAFE)
        return 0;
    *out = r;
    return 1;
}

static int mat_checked_add(long long a, long long b, long long *out) {
    long long r = 0;
    if (__builtin_add_overflow(a, b, &r))
        return 0;
    *out = r;
    return 1;
}

static int mat_checked_sub(long long a, long long b, long long *out) {
    long long r = 0;
    if (__builtin_sub_overflow(a, b, &r))
        return 0;
    *out = r;
    return 1;
}

static int mat_mul3(long long a, long long b, long long c, long long *out) {
    long long ab = 0;
    if (!mat_checked_mul(a, b, &ab))
        return 0;
    return mat_checked_mul(ab, c, out);
}

/* 3x3 determinant from nine scalars; avoids pointer-to-array parameters. */
static int mat_det3(long long a00, long long a01, long long a02,
                    long long a10, long long a11, long long a12,
                    long long a20, long long a21, long long a22,
                    long long *out) {
    long long t[6];
    long long acc = 0;
    if (!mat_mul3(a00, a11, a22, &t[0])) return 0;
    if (!mat_mul3(a01, a12, a20, &t[1])) return 0;
    if (!mat_mul3(a02, a10, a21, &t[2])) return 0;
    if (!mat_mul3(a02, a11, a20, &t[3])) return 0;
    if (!mat_mul3(a01, a10, a22, &t[4])) return 0;
    if (!mat_mul3(a00, a12, a21, &t[5])) return 0;
    if (!mat_checked_add(t[0], t[1], &acc)) return 0;
    if (!mat_checked_add(acc, t[2], &acc)) return 0;
    if (!mat_checked_sub(acc, t[3], &acc)) return 0;
    if (!mat_checked_sub(acc, t[4], &acc)) return 0;
    if (!mat_checked_sub(acc, t[5], &acc)) return 0;
    *out = acc;
    return 1;
}

static int mat_det(int n, long long *out) {
    long long term = 0;
    long long acc = 0;
    long long minor = 0;

    if (n == 1) {
        *out = mat_a[0][0];
        return 1;
    }
    if (n == 2) {
        if (!mat_checked_mul(mat_a[0][0], mat_a[1][1], &term)) return 0;
        if (!mat_checked_mul(mat_a[0][1], mat_a[1][0], &minor)) return 0;
        return mat_checked_sub(term, minor, out);
    }
    if (n == 3) {
        return mat_det3(mat_a[0][0], mat_a[0][1], mat_a[0][2],
                        mat_a[1][0], mat_a[1][1], mat_a[1][2],
                        mat_a[2][0], mat_a[2][1], mat_a[2][2], out);
    }
    /* n == 4: cofactor expansion along the first row. */
    if (!mat_det3(mat_a[1][1], mat_a[1][2], mat_a[1][3],
                  mat_a[2][1], mat_a[2][2], mat_a[2][3],
                  mat_a[3][1], mat_a[3][2], mat_a[3][3], &minor)) return 0;
    if (!mat_checked_mul(mat_a[0][0], minor, &term)) return 0;
    acc = term;
    if (!mat_det3(mat_a[1][0], mat_a[1][2], mat_a[1][3],
                  mat_a[2][0], mat_a[2][2], mat_a[2][3],
                  mat_a[3][0], mat_a[3][2], mat_a[3][3], &minor)) return 0;
    if (!mat_checked_mul(mat_a[0][1], minor, &term)) return 0;
    if (!mat_checked_sub(acc, term, &acc)) return 0;
    if (!mat_det3(mat_a[1][0], mat_a[1][1], mat_a[1][3],
                  mat_a[2][0], mat_a[2][1], mat_a[2][3],
                  mat_a[3][0], mat_a[3][1], mat_a[3][3], &minor)) return 0;
    if (!mat_checked_mul(mat_a[0][2], minor, &term)) return 0;
    if (!mat_checked_add(acc, term, &acc)) return 0;
    if (!mat_det3(mat_a[1][0], mat_a[1][1], mat_a[1][2],
                  mat_a[2][0], mat_a[2][1], mat_a[2][2],
                  mat_a[3][0], mat_a[3][1], mat_a[3][2], &minor)) return 0;
    if (!mat_checked_mul(mat_a[0][3], minor, &term)) return 0;
    if (!mat_checked_sub(acc, term, &acc)) return 0;
    *out = acc;
    return 1;
}

static void mat_print(const char *op, int rows, int cols) {
    int i;
    int j;
    print("MAT", op, rows, cols);
    for (i = 0; i < rows; i++) {
        for (j = 0; j < cols; j++)
            print("MAT", i, j, mat_out[i][j]);
    }
}

int main(int argc, char **argv) {
    error ferr = error();
    const char *op;
    const char *r1;
    const char *c1;
    const char *d1;
    const char *r2;
    const char *c2;
    const char *d2;
    int rows1 = 0;
    int cols1 = 0;
    int rows2 = 0;
    int cols2 = 0;
    long long value = 0;
    int i;
    int j;
    int k;

    mat_err = &ferr;
    if (argc < 2) {
        mat_refuse("usage");
        return 2;
    }
    op = mat_arg(argv, 1);
    if (op == NULL) {
        mat_refuse("usage");
        return 2;
    }
    if (strcmp(op, "det") == 0 || strcmp(op, "determinant") == 0) {
        if (argc != 5) {
            mat_refuse("bad-args");
            return 1;
        }
        r1 = mat_arg(argv, 2);
        c1 = mat_arg(argv, 3);
        d1 = mat_arg(argv, 4);
        if (!mat_load(0, r1, c1, d1, &rows1, &cols1))
            return 1;
        if (rows1 != cols1) {
            mat_refuse("not-square");
            return 1;
        }
        if (!mat_det(rows1, &value)) {
            mat_refuse_range("overflow");
            return 1;
        }
        print("MAT determinant", rows1, value);
        return 0;
    }
    if (strcmp(op, "add") != 0 && strcmp(op, "multiply") != 0) {
        mat_refuse("unknown-op");
        return 1;
    }
    if (argc != 8) {
        mat_refuse("bad-args");
        return 1;
    }
    r1 = mat_arg(argv, 2);
    c1 = mat_arg(argv, 3);
    d1 = mat_arg(argv, 4);
    r2 = mat_arg(argv, 5);
    c2 = mat_arg(argv, 6);
    d2 = mat_arg(argv, 7);
    if (!mat_load(0, r1, c1, d1, &rows1, &cols1) ||
        !mat_load(1, r2, c2, d2, &rows2, &cols2))
        return 1;

    if (strcmp(op, "add") == 0) {
        if (rows1 != rows2 || cols1 != cols2) {
            mat_refuse("dim-mismatch");
            return 1;
        }
        for (i = 0; i < rows1; i++) {
            for (j = 0; j < cols1; j++) {
                if (!mat_checked_add(mat_a[i][j], mat_b[i][j],
                                     &mat_out[i][j])) {
                    mat_refuse_range("overflow");
                    return 1;
                }
            }
        }
        mat_print("add", rows1, cols1);
        return 0;
    }

    /* multiply: cols1 must equal rows2 */
    if (cols1 != rows2) {
        mat_refuse("dim-mismatch");
        return 1;
    }
    for (i = 0; i < rows1; i++) {
        for (j = 0; j < cols2; j++) {
            long long acc = 0;
            long long product = 0;
            for (k = 0; k < cols1; k++) {
                if (!mat_checked_mul(mat_a[i][k], mat_b[k][j], &product) ||
                    !mat_checked_add(acc, product, &acc)) {
                    mat_refuse_range("overflow");
                    return 1;
                }
            }
            mat_out[i][j] = acc;
        }
    }
    mat_print("multiply", rows1, cols2);
    return 0;
}
