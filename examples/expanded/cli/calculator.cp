/* calculator.cp -- deterministic integer expression evaluator in C+.
 *
 * Build (from the repository root):
 *   ./build/c+ build examples/expanded/cli/calculator.cp --strict --sanitize=none -I. \
 *       -o examples/expanded/cli/.run/calculator
 *
 * Usage:
 *   calculator "1+2*3"
 *   printf '7/(2+2)\n' | calculator          (one expression from stdin)
 *
 * Grammar: expr = term { ('+'|'-') term }
 *          term = factor { ('*'|'/') factor }
 *          factor = [+-]? integer | '(' expr ')'
 *
 * Division by zero is refused through the checked `error *` API; no variable
 * divisor is ever passed to C's `/` operator (the strict checker rejects
 * unverified division, so a shift-and-subtract helper is used instead).
 * Operands and intermediates are bounded to +/-1e9 and overflow is refused.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/vector.hp"
#include "stdlib/string.hp"
#include "stdlib/io.hp"

#define CALC_SRC_CAP 256
#define CALC_LIMIT   1000000000LL

static char     calc_src[CALC_SRC_CAP];
static int      calc_pos;
static error   *calc_err;
static vector  *calc_ops;

static char *calc_arg(char **p, size_t idx) {
    char *value;
    __builtin_memcpy(&value, p + idx, sizeof(value));
    return value;
}

static void calc_fail(const char *what) {
    if (calc_err != NULL)
        error_fail(calc_err, CPLUS_EXIT_UNSUPPORTED, what);
}

static void calc_skip(void) {
    while (calc_src[calc_pos] == ' ' || calc_src[calc_pos] == '\t')
        calc_pos++;
}

/* Exact unsigned division without a variable-divisor expression.
 * Magnitudes are bounded to 1e9, so 32-bit unsigned arithmetic suffices. */
static unsigned int calc_udiv(unsigned int a, unsigned int b) {
    unsigned int q = 0;
    unsigned int r = a;
    for (int sh = 31; sh >= 0; --sh) {
        unsigned int d = 1u << sh;
        if (b <= (UINT_MAX >> sh)) {
            unsigned int v = b << sh;
            if (r >= v) {
                r -= v;
                q += d;
            }
        }
    }
    return q;
}

/* Signed division; magnitudes are <= 1e9 so negation cannot overflow. */
static long long calc_div(long long a, long long b) {
    unsigned int ua = a < 0 ? (unsigned int)(-a) : (unsigned int)a;
    unsigned int ub = b < 0 ? (unsigned int)(-b) : (unsigned int)b;
    unsigned int q = calc_udiv(ua, ub);
    return ((a < 0) != (b < 0)) ? -(long long)q : (long long)q;
}

static int calc_factor(long long *out);
static int calc_expr(long long *out);

static int calc_number(long long *out) {
    long long v = 0;
    int digits = 0;
    while (calc_src[calc_pos] >= '0' && calc_src[calc_pos] <= '9') {
        int d = calc_src[calc_pos] - '0';
        if (v > 100000000LL) {
            calc_fail("overflow");
            return 0;
        }
        v = v * 10 + d;
        digits++;
        calc_pos++;
    }
    if (digits == 0 || v > CALC_LIMIT) {
        calc_fail(digits == 0 ? "syntax" : "overflow");
        return 0;
    }
    *out = v;
    return 1;
}

static int calc_factor(long long *out) {
    long long v = 0;
    calc_skip();
    if (calc_src[calc_pos] == '+' || calc_src[calc_pos] == '-') {
        int negative = calc_src[calc_pos] == '-';
        calc_pos++;
        if (!calc_factor(&v))
            return 0;
        v = negative ? -v : v;
    } else if (calc_src[calc_pos] == '(') {
        calc_pos++;
        if (!calc_expr(&v))
            return 0;
        calc_skip();
        if (calc_src[calc_pos] != ')') {
            calc_fail("syntax");
            return 0;
        }
        calc_pos++;
    } else {
        if (!calc_number(&v))
            return 0;
    }
    *out = v;
    return 1;
}

static int calc_term(long long *out) {
    long long a = 0;
    long long b = 0;
    if (!calc_factor(&a))
        return 0;
    for (;;) {
        char op;
        calc_skip();
        op = calc_src[calc_pos];
        if (op == '*') {
            calc_pos++;
            if (!calc_factor(&b))
                return 0;
            a = a * b; /* |a|,|b| <= 1e9: product fits in long long */
            if (calc_ops != NULL)
                calc_ops->push_back(1);
        } else if (op == '/') {
            calc_pos++;
            if (!calc_factor(&b))
                return 0;
            if (b == 0) {
                calc_fail("divide-by-zero");
                return 0;
            }
            a = calc_div(a, b);
            if (calc_ops != NULL)
                calc_ops->push_back(2);
        } else {
            break;
        }
        if (a > CALC_LIMIT || a < -CALC_LIMIT) {
            calc_fail("overflow");
            return 0;
        }
    }
    *out = a;
    return 1;
}

static int calc_expr(long long *out) {
    long long a = 0;
    long long b = 0;
    if (!calc_term(&a))
        return 0;
    for (;;) {
        char op;
        calc_skip();
        op = calc_src[calc_pos];
        if (op == '+') {
            calc_pos++;
            if (!calc_term(&b))
                return 0;
            a = a + b;
            if (calc_ops != NULL)
                calc_ops->push_back(3);
        } else if (op == '-') {
            calc_pos++;
            if (!calc_term(&b))
                return 0;
            a = a - b;
            if (calc_ops != NULL)
                calc_ops->push_back(4);
        } else {
            break;
        }
        if (a > CALC_LIMIT || a < -CALC_LIMIT) {
            calc_fail("overflow");
            return 0;
        }
    }
    *out = a;
    return 1;
}

int main(int argc, char **argv) {
    vector calc_op_log = vector();
    error cerr = error();
    long long result = 0;
    int ok;

    if (argc > 1) {
        string text = string(calc_arg(argv, 1));
        if (!text.valid() || text.size() + 1 > sizeof(calc_src)) {
            print("CALC ERROR input-too-long");
            return 1;
        }
        if (text.substr(0, text.size(), calc_src, sizeof(calc_src)) != 0) {
            text.drop();
            print("CALC ERROR input-copy");
            return 1;
        }
        text.drop();
    } else {
        error ierr = error();
        int rr = read_line(calc_src, sizeof(calc_src), &ierr);
        if (rr != STDLIB_OK) {
            print("CALC ERROR no-input");
            return 1;
        }
    }

    calc_pos = 0;
    calc_err = &cerr;
    calc_ops = &calc_op_log;
    ok = calc_expr(&result);
    if (ok) {
        calc_skip();
        if (calc_src[calc_pos] != '\0') {
            calc_fail("syntax");
            ok = 0;
        }
    }
    if (!ok) {
        print("CALC ERROR", cerr.what());
        calc_op_log.drop();
        return 1;
    }

    print("CALC", calc_src, "=", result);
    print("CALC ops", (int)calc_op_log.size());
    calc_op_log.drop();
    return 0;
}
