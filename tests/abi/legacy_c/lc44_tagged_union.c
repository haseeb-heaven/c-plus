/*
 * lc44 - union patterns: tagged union with memcpy-based payload writes.
 *
 * Independent legacy-C ABI probe: avoids type-punning UB by writing union
 * members through typed accessors and reading back the active member only.
 */
#include <stdio.h>
#include <string.h>

enum lc44_kind { LC44_INT = 1, LC44_DOUBLE = 2, LC44_BYTES = 3 };

struct lc44_value {
    enum lc44_kind kind;
    union {
        int i;
        double d;
        unsigned char bytes[8];
    } u;
};

static void set_int(struct lc44_value *v, int x)
{
    v->kind = LC44_INT;
    v->u.i = x;
}

static void set_double(struct lc44_value *v, double x)
{
    v->kind = LC44_DOUBLE;
    v->u.d = x;
}

static void set_bytes(struct lc44_value *v, const unsigned char *p)
{
    v->kind = LC44_BYTES;
    memset(v->u.bytes, 0, sizeof(v->u.bytes));
    memcpy(v->u.bytes, p, 4);
}

int main(void)
{
    struct lc44_value a, b, c;
    const unsigned char seed[4] = { 1, 2, 3, 4 };
    int ok = 1;

    set_int(&a, 44);
    set_double(&b, 2.75);
    set_bytes(&c, seed);

    if (a.kind != LC44_INT || a.u.i != 44 ||
        b.kind != LC44_DOUBLE || b.u.d != 2.75 ||
        c.kind != LC44_BYTES || c.u.bytes[0] != 1u ||
        c.u.bytes[3] != 4u || c.u.bytes[4] != 0u) {
        ok = 0;
    }
    if (sizeof(a.u) < sizeof(a.u.i) || sizeof(a.u) < sizeof(a.u.d)) {
        ok = 0;
    }
    if (!ok) {
        printf("lc44 result: FAIL i=%d d=%.2f b0=%u\n",
               a.u.i, b.u.d, (unsigned int)c.u.bytes[0]);
        return 1;
    }
    printf("lc44 result: OK i=%d d=%.2f b0=%u usize=%lu\n",
           a.u.i, b.u.d, (unsigned int)c.u.bytes[0],
           (unsigned long)sizeof(a.u));
    return 0;
}
