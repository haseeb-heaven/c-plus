/* bigint.cp -- fixed-width big integer arithmetic with explicit refusal.
 *
 * Build (from the repository root):
 *   ./build/c+ build apps/legacy_examples/expanded/math_2/bigint.cp --strict --sanitize=none -I. \
 *       -o apps/legacy_examples/expanded/math_2/.run/bigint
 *
 * Invariants (all enforced at run time, all observable in the transcript):
 *   - values are little-endian base-10 digit arrays with at most BI_CAP digits;
 *   - an operand longer than BI_CAP is refused (overflow-input);
 *   - a non-digit character is refused (digit-syntax);
 *   - addition/multiplication whose exact result needs more than BI_CAP digits
 *     is refused (overflow-result);
 *   - subtraction of a larger operand is refused (underflow) rather than
 *     silently producing a negative value.
 *
 * No variable divisor/modulus is used: only the named constant DEC_BASE and
 * per-digit carries are involved.  This is a teaching model with run-time
 * checks, NOT a formal proof of correctness.
 */
#include <stdio.h>
#include <string.h>

#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/vector.hp"
#include "stdlib/string.hp"

#define BI_CAP    16
#define DEC_BASE  10

typedef struct {
    int digit[BI_CAP];   /* digit[0] is the least significant digit */
    int ndig;             /* 1 <= ndig <= BI_CAP for a normalised value */
} bigint;

static error *bi_err;

static void bi_refuse(const char *what) {
    if (bi_err != NULL)
        error_fail(bi_err, CPLUS_EXIT_BOUNDS, what);
}

/* Copy a decimal literal into a bounded digit array.  Refuses empty input,
 * over-capacity input and non-digit bytes before writing any digit. */
static int bi_load(const char text[BI_CAP + 2], size_t n, bigint *out) {
    char buf[BI_CAP + 2];
    size_t i;
    if (n > BI_CAP) {
        bi_refuse("overflow-input");
        return 0;
    }
    if (n == 0 || n + 1 > sizeof(buf)) {
        bi_refuse("overflow-input");
        return 0;
    }
    __builtin_memcpy(buf, text, n);
    buf[n] = '\0';
    for (i = 0; i < n; i++) {
        if (buf[i] < '0' || buf[i] > '9') {
            bi_refuse("digit-syntax");
            return 0;
        }
    }
    out->ndig = (int)n;
    for (i = 0; i < n; i++)
        out->digit[i] = buf[(n - 1) - i] - '0';
    return 1;
}

static void bi_normalize(bigint *a) {
    while (a->ndig > 1 && a->digit[a->ndig - 1] == 0)
        a->ndig--;
}

static int bi_cmp(const bigint *a, const bigint *b) {
    int i;
    if (a->ndig != b->ndig)
        return a->ndig < b->ndig ? -1 : 1;
    for (i = a->ndig - 1; i >= 0; i--) {
        if (a->digit[i] != b->digit[i])
            return a->digit[i] < b->digit[i] ? -1 : 1;
    }
    return 0;
}

static int bi_add(const bigint *a, const bigint *b, bigint *r) {
    int i, carry = 0;
    int max = a->ndig > b->ndig ? a->ndig : b->ndig;
    r->ndig = 0;
    for (i = 0; i < max; i++) {
        int s = carry;
        if (i < a->ndig) s += a->digit[i];
        if (i < b->ndig) s += b->digit[i];
        r->digit[i] = s % DEC_BASE;
        carry = s / DEC_BASE;
    }
    r->ndig = max;
    if (carry != 0) {
        if (max + 1 > BI_CAP) {
            bi_refuse("overflow-result");
            return 0;
        }
        r->digit[max] = carry;
        r->ndig = max + 1;
    }
    bi_normalize(r);
    return 1;
}

static int bi_sub(const bigint *a, const bigint *b, bigint *r) {
    int i, borrow = 0;
    if (bi_cmp(a, b) < 0) {
        bi_refuse("underflow");
        return 0;
    }
    r->ndig = a->ndig;
    for (i = 0; i < a->ndig; i++) {
        int d = a->digit[i] - borrow;
        if (i < b->ndig) d -= b->digit[i];
        if (d < 0) {
            d += DEC_BASE;
            borrow = 1;
        } else {
            borrow = 0;
        }
        r->digit[i] = d;
    }
    bi_normalize(r);
    return 1;
}

static int bi_mul(const bigint *a, const bigint *b, bigint *r) {
    int i, j;
    int total = a->ndig + b->ndig;
    if (total > BI_CAP) {
        bi_refuse("overflow-result");
        return 0;
    }
    r->ndig = total;
    for (i = 0; i < total; i++)
        r->digit[i] = 0;
    for (i = 0; i < a->ndig; i++) {
        int carry = 0;
        int pos;
        for (j = 0; j < b->ndig; j++) {
            int v;
            pos = i + j;
            if (pos >= BI_CAP) {
                bi_refuse("overflow-result");
                return 0;
            }
            v = r->digit[pos] + a->digit[i] * b->digit[j] + carry;
            r->digit[pos] = v % DEC_BASE;
            carry = v / DEC_BASE;
        }
        pos = i + b->ndig;
        while (carry != 0) {
            int v;
            if (pos >= BI_CAP) {
                bi_refuse("overflow-result");
                return 0;
            }
            v = r->digit[pos] + carry;
            r->digit[pos] = v % DEC_BASE;
            carry = v / DEC_BASE;
            pos++;
        }
    }
    bi_normalize(r);
    return 1;
}

static void bi_text(const bigint *a, char *out, size_t out_size) {
    char tmp[BI_CAP];
    int i, k = 0;
    for (i = a->ndig - 1; i >= 0; i--)
        tmp[k++] = (char)('0' + a->digit[i]);
    if ((size_t)k >= out_size)
        k = (int)out_size - 1;
    for (i = 0; i < k; i++)
        out[i] = tmp[i];
    out[k] = '\0';
}

/* One fully deterministic operation; appends its line to `line` and records
 * the result digit count in `log`.  0 means a refusal already was recorded. */
static int bi_case(const char *op, const char *sa, const char *sb,
                   string *line, vector *log) {
    bigint a, b, r;
    char ta[BI_CAP + 2], tb[BI_CAP + 2], tr[BI_CAP + 2];
    const char *symbol;

    if (!bi_load(sa, strlen(sa), &a)) return 0;
    if (!bi_load(sb, strlen(sb), &b)) return 0;

    if (strcmp(op, "add") == 0) {
        if (!bi_add(&a, &b, &r)) return 0;
        symbol = " + ";
    } else if (strcmp(op, "sub") == 0) {
        if (!bi_sub(&a, &b, &r)) return 0;
        symbol = " - ";
    } else if (strcmp(op, "mul") == 0) {
        if (!bi_mul(&a, &b, &r)) return 0;
        symbol = " * ";
    } else {
        bi_refuse("bad-op");
        return 0;
    }

    bi_text(&a, ta, sizeof(ta));
    bi_text(&b, tb, sizeof(tb));
    bi_text(&r, tr, sizeof(tr));

    line->append("BIGINT ");
    line->append(op);
    line->append(" ");
    line->append(ta);
    line->append(symbol);
    line->append(tb);
    line->append(" = ");
    line->append(tr);
    if (log != NULL)
        log->push_back(r.ndig);
    print(line->c_str());
    line->clear();
    return 1;
}

static void bi_print_refusal(error *e) {
    print("BIGINT REFUSED code=", e->code(), e->what());
}

static char *bi_arg(char **p, size_t idx) {
    char *value;
    __builtin_memcpy(&value, p + idx, sizeof(value));
    return value;
}

int main(int argc, char **argv) {
    error e = error();
    vector log = vector();
    string line = string("");
    string mode = string("");
    int ok = 1;

    bi_err = &e;

    if (argc > 1) {
        char *m = bi_arg(argv, 1);
        mode = string(m);
        if (!mode.valid() || mode.size() > 32) {
            print("BIGINT REFUSED code=101 overflow-input");
            return 1;
        }
    }

    if (argc <= 1) {
        /* Fixed vectors: result length stays within BI_CAP. */
        ok = bi_case("add", "999999999999999", "1", &line, &log) &&
             bi_case("add", "1234567890123456", "7", &line, &log) &&
             bi_case("sub", "1000000000000000", "1", &line, &log) &&
             bi_case("mul", "99999", "99999", &line, &log) &&
             bi_case("mul", "1234567", "89", &line, &log);
        if (ok)
            print("BIGINT OK count=5 cap=16 ops=", (int)log.size());
    } else if (mode.equals("input-overflow")) {
        ok = bi_case("add", "10000000000000000", "1", &line, &log); /* 17 digits */
    } else if (mode.equals("result-overflow")) {
        ok = bi_case("add", "9999999999999999", "1", &line, &log); /* carry */
    } else if (mode.equals("sub-underflow")) {
        ok = bi_case("sub", "1", "2", &line, &log);
    } else if (mode.equals("mul-overflow")) {
        ok = bi_case("mul", "999999999", "999999999", &line, &log);
    } else if (mode.equals("bad-digit")) {
        ok = bi_case("add", "12x4", "1", &line, &log);
    } else {
        error_fail(&e, CPLUS_EXIT_UNSUPPORTED, "bad-mode");
        ok = 0;
    }

    if (!ok) {
        if (e.failed_now())
            bi_print_refusal(&e);
        else
            print("BIGINT REFUSED code=108 internal");
        line.drop();
        log.drop();
        mode.drop();
        return 1;
    }

    line.drop();
    log.drop();
    mode.drop();
    return 0;
}
