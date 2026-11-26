/* Independent modern-C port of the bignum sample workload.
 * C11. Transient limb buffers come from a scoped bump arena (data-stack model).
 * No source from the inspiration language was copied. */
#include "common.h"
#include <stdint.h>

/* Bump arena: allocations are transient; a saved top is restored in one store. */
static unsigned char arena_bytes[1 << 16];
static size_t arena_top;
static void *scoped_take(size_t n) {
    void *p = arena_bytes + arena_top;
    arena_top = (arena_top + n + 15u) & ~(size_t)15u;
    return p;
}
static size_t scoped_mark(void) { return arena_top; }
static void scoped_drop(size_t mark) { arena_top = mark; }

#define NLIMB 64
typedef struct { int n; uint32_t d[NLIMB]; } Big;

static void bn_norm(Big *a) { while (a->n > 0 && a->d[a->n - 1] == 0) a->n--; }
static void bn_set(Big *a, uint32_t v) { a->n = v ? 1 : 0; a->d[0] = v; }

static void bn_copy(Big *r, const Big *a) {
    r->n = a->n;
    memcpy(r->d, a->d, (size_t)a->n * sizeof(uint32_t));
}

static void bn_mul_small(Big *a, uint32_t m) {
    uint64_t carry = 0;
    int i;
    if (m == 0) { a->n = 0; return; }
    for (i = 0; i < a->n; ++i) {
        uint64_t t = (uint64_t)a->d[i] * m + carry;
        a->d[i] = (uint32_t)t;
        carry = t >> 32;
    }
    while (carry) { a->d[a->n++] = (uint32_t)carry; carry >>= 32; }
}

static void bn_add(Big *r, const Big *a, const Big *b) {
    Big t;
    int n = a->n > b->n ? a->n : b->n, i;
    uint64_t carry = 0;
    memset(&t, 0, sizeof(t));
    for (i = 0; i < n; ++i) {
        uint64_t s = carry + (i < a->n ? a->d[i] : 0u) + (i < b->n ? b->d[i] : 0u);
        t.d[i] = (uint32_t)s;
        carry = s >> 32;
    }
    t.n = n;
    if (carry) t.d[t.n++] = (uint32_t)carry;
    bn_norm(&t);
    *r = t;
}

static void bn_mul(Big *r, const Big *a, const Big *b) {
    Big t;
    int i, j, k;
    memset(&t, 0, sizeof(t));
    t.n = a->n + b->n > NLIMB ? NLIMB : a->n + b->n;
    for (i = 0; i < a->n; ++i) {
        uint64_t carry = 0;
        for (j = 0; j < b->n; ++j) {
            uint64_t prod = (uint64_t)a->d[i] * b->d[j];
            uint64_t sum = (uint64_t)t.d[i + j] + (uint32_t)prod + carry;
            t.d[i + j] = (uint32_t)sum;
            carry = (prod >> 32) + (sum >> 32);
        }
        k = i + b->n;
        while (carry && k < NLIMB) {
            uint64_t sum = (uint64_t)t.d[k] + carry;
            t.d[k] = (uint32_t)sum;
            carry = sum >> 32;
            ++k;
        }
    }
    bn_norm(&t);
    *r = t;
}

static uint32_t bn_div_small(Big *a, uint32_t d) {
    uint64_t rem = 0;
    int i;
    for (i = a->n - 1; i >= 0; --i) {
        uint64_t cur = (rem << 32) | a->d[i];
        a->d[i] = (uint32_t)(cur / d);
        rem = cur % d;
    }
    bn_norm(a);
    return (uint32_t)rem;
}

static void bn_decimal(const Big *a, char *out) {
    Big t;
    char rev[1100];
    int len = 0, i;
    bn_copy(&t, a);
    if (t.n == 0) { strcpy(out, "0"); return; }
    while (t.n) rev[len++] = (char)('0' + bn_div_small(&t, 10));
    for (i = 0; i < len; ++i) out[i] = rev[len - 1 - i];
    out[len] = 0;
}

int main(void) {
    size_t mark;
    Big *fact, *a, *b, *tmp, *pow2, *sq, *q;
    char s[1100];
    int digits;

    (void)fails; (void)report; /* common.h is shared by all ports */
    mark = scoped_mark();
    fact = scoped_take(sizeof(Big));
    a = scoped_take(sizeof(Big));
    b = scoped_take(sizeof(Big));
    tmp = scoped_take(sizeof(Big));
    pow2 = scoped_take(sizeof(Big));
    sq = scoped_take(sizeof(Big));
    q = scoped_take(sizeof(Big));

    bn_set(fact, 1);
    for (int i = 2; i <= 100; ++i) bn_mul_small(fact, (uint32_t)i);
    bn_decimal(fact, s);
    printf("100! = %s\n", s);
    printf("  %d limbs, %lu digits\n", fact->n, (unsigned long)strlen(s));

    bn_set(a, 0);
    bn_set(b, 1);
    for (int i = 2; i <= 500; ++i) {
        bn_add(tmp, a, b);
        bn_copy(a, b);
        bn_copy(b, tmp);
    }
    bn_decimal(b, s);
    printf("fib(500) = %s\n", s);

    bn_set(pow2, 1);
    for (int i = 0; i < 1000; ++i) bn_mul_small(pow2, 2);
    bn_decimal(pow2, s);
    digits = (int)strlen(s);
    printf("2^1000 has %d digits and ends in ...%.*s\n", digits, 12, s + digits - 12);

    bn_set(tmp, 1);
    for (int i = 0; i < 500; ++i) bn_mul_small(tmp, 2);
    bn_mul(sq, tmp, tmp);
    printf("2^500 squared matches: %s\n",
           sq->n == pow2->n && memcmp(sq->d, pow2->d, (size_t)sq->n * 4) == 0 ? "true" : "false");

    bn_copy(q, fact);
    bn_div_small(q, 9900);
    bn_set(tmp, 1);
    for (int i = 2; i <= 98; ++i) bn_mul_small(tmp, (uint32_t)i);
    printf("100! / 9900 == 98!: %s\n",
           q->n == tmp->n && memcmp(q->d, tmp->d, (size_t)q->n * 4) == 0 ? "true" : "false");

    scoped_drop(mark);
    return 0;
}
