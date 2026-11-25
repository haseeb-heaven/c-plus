/* prng.cp -- deterministic xorshift32 PRNG with capacity/overflow refusal.
 *
 * Build (from the repository root):
 *   ./build/c+ build examples/expanded/math_2/prng.cp --strict --sanitize=none -I. \
 *       -o examples/expanded/math_2/.run/prng
 *
 * Usage:
 *   prng                     fixed seed 305419896 (0x12345678), 16 values
 *   prng <seed> <count>      explicit decimal seed and value count
 *
 * Refusals (all printed as a deterministic line, exit 1):
 *   seed > 2^32-1            -> seed-overflow   (CPLUS_EXIT_BOUNDS, code 101)
 *   count > COUNT_CAP (256)  -> count-too-large (CPLUS_EXIT_BOUNDS, code 101)
 *   non-digit seed/count     -> seed-syntax / count-syntax (code 108)
 *   count > 2^64-1           -> count-overflow  (code 101)
 *
 * Bounded values use a power-of-two range (8) and a bitmask, so no variable
 * division or modulus appears.  Teaching model, NOT a formal proof.
 */
#include <stdio.h>
#include <string.h>
#include <limits.h>

#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/vector.hp"
#include "stdlib/string.hp"

#define COUNT_CAP 256u
#define SEED_CAP  4294967295ULL
#define BOUND_MASK 0x7u          /* bounded value = raw & 7, range [0,7] */
#define DEC_BASE   10ULL
#define PR_ARG_CAP  64u

static error *pr_err;

static void pr_refuse_bounds(const char *what) {
    if (pr_err != NULL)
        error_fail(pr_err, CPLUS_EXIT_BOUNDS, what);
}
static void pr_refuse_syntax(const char *what) {
    if (pr_err != NULL)
        error_fail(pr_err, CPLUS_EXIT_UNSUPPORTED, what);
}

/* Strict decimal parser: the guard uses only a named constant divisor and
 * subtraction, so neither the comparison nor the accumulation can wrap. */
static int pr_parse(const char *text, unsigned long long limit,
                    const char *why_syntax, const char *why_overflow,
                    unsigned long long *out) {
    unsigned long long acc = 0;
    size_t i, n = strlen(text);
    if (n > PR_ARG_CAP) {
        pr_refuse_syntax(why_syntax);
        return 0;
    }
    if (n == 0) {
        pr_refuse_syntax(why_syntax);
        return 0;
    }
    {
        char local[PR_ARG_CAP + 2];
        __builtin_memcpy(local, text, n);
        local[n] = '\0';
        for (i = 0; i < n; i++) {
            char c = local[i];
            unsigned int d;
            if (c < '0' || c > '9') {
                pr_refuse_syntax(why_syntax);
                return 0;
            }
            d = (unsigned int)(c - '0');
            if ((unsigned long long)d > limit ||
                acc > (limit - (unsigned long long)d) / DEC_BASE) {
                pr_refuse_bounds(why_overflow);
                return 0;
            }
            acc = acc * DEC_BASE + (unsigned long long)d;
        }
    }
    *out = acc;
    return 1;
}

/* xorshift32: three xor/shift steps, all on a 32-bit state. */
static unsigned int pr_next(unsigned int *state) {
    unsigned int x = *state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *state = x;
    return x;
}

static char *pr_arg(char **p, size_t idx) {
    char *value;
    __builtin_memcpy(&value, p + idx, sizeof(value));
    return value;
}

/* Decimal formatting of a 32-bit value using the named constant DEC_BASE. */
static void pr_dec(unsigned int v, char *out, size_t out_size) {
    char tmp[16];
    int k = 0, i = 0;
    unsigned int x = v;
    if (x == 0)
        tmp[k++] = '0';
    while (x != 0) {
        tmp[k++] = (char)('0' + (x % DEC_BASE));
        x = x / DEC_BASE;
    }
    while (k > 0 && i < (int)out_size - 1)
        out[i++] = tmp[--k];
    out[i] = '\0';
}

static void pr_emit(vector *values, string *out, unsigned int seed) {
    size_t i;
    unsigned int bounded_sum = 0;
    unsigned int state = seed;
    char num[24];

    out->append("PRNG seed=");
    pr_dec(seed, num, sizeof(num));
    out->append(num);
    out->append(" count=");
    pr_dec((unsigned int)values->size(), num, sizeof(num));
    out->append(num);
    out->append(" cap=256");
    print(out->c_str());
    out->clear();

    for (i = 0; i < values->size(); i++) {
        int raw = 0;
        unsigned int b;
        if (values->at_checked(i, &raw) != 0)
            return;
        b = ((unsigned int)raw) & BOUND_MASK;
        bounded_sum += b;
        out->append("PRNG ");
        pr_dec((unsigned int)i, num, sizeof(num));
        out->append(num);
        out->append(" raw=");
        pr_dec((unsigned int)raw, num, sizeof(num));
        out->append(num);
        out->append(" bounded=");
        pr_dec(b, num, sizeof(num));
        out->append(num);
        print(out->c_str());
        out->clear();
    }
    out->append("PRNG sum=");
    pr_dec(bounded_sum, num, sizeof(num));
    out->append(num);
    out->append(" OK");
    print(out->c_str());
    out->clear();

    /* Regenerate from the same seed to prove the transcript is reproducible. */
    state = seed;
    for (i = 0; i < values->size(); i++) {
        unsigned int r = pr_next(&state);
        int stored = 0;
        if (values->at_checked(i, &stored) != 0 ||
            (unsigned int)stored != r) {
            error_fail(pr_err, CPLUS_EXIT_UNSUPPORTED, "replay-mismatch");
            return;
        }
    }
}

/* All fallible work lives here so main needs no goto and always drops its
 * owned containers on both the success and refusal paths. */
static int pr_run(vector *values, string *line, int argc, char **argv) {
    unsigned long long seed = 305419896ULL;   /* 0x12345678 */
    unsigned long long count = 16ULL;
    unsigned int state;
    unsigned long long i;

    if (argc == 3) {
        char *s = pr_arg(argv, 1);
        char *c = pr_arg(argv, 2);
        if (!pr_parse(s, SEED_CAP, "seed-syntax", "seed-overflow", &seed))
            return 0;
        if (!pr_parse(c, ULLONG_MAX, "count-syntax", "count-overflow", &count))
            return 0;
        if (count > (unsigned long long)COUNT_CAP) {
            pr_refuse_bounds("count-too-large");
            return 0;
        }
    } else if (argc != 1) {
        pr_refuse_syntax("usage");
        return 0;
    }

    state = (unsigned int)seed;
    for (i = 0; i < count; i++) {
        unsigned int raw = pr_next(&state);
        if (values->push_back((int)raw) != 0) {
            pr_refuse_bounds("capacity");
            return 0;
        }
    }

    pr_emit(values, line, (unsigned int)seed);
    return pr_err->failed_now() ? 0 : 1;
}

int main(int argc, char **argv) {
    error e = error();
    vector values = vector();
    string line = string("");
    int ok;

    pr_err = &e;
    ok = pr_run(&values, &line, argc, argv);
    if (!ok) {
        if (e.failed_now())
            print("PRNG REFUSED code=", e.code(), e.what());
        else
            print("PRNG REFUSED code=108 internal");
    }
    values.drop();
    line.drop();
    return ok ? 0 : 1;
}
