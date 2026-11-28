/*
 * lc31 - opaque handles: ref-counted-style state machine behind an incomplete
 * type. Exercises handle aliasing (two pointers, one object) and deterministic
 * teardown, without exposing layout to callers.
 */
#include <stdio.h>
#include <stdlib.h>

struct lc31_obj;

struct lc31_obj {
    unsigned int tag;
    int state;
    int refs;
};

#define LC31_TAG 0xC31u

struct lc31_obj *lc31_open(void)
{
    struct lc31_obj *o = (struct lc31_obj *)malloc(sizeof(*o));
    if (o == NULL) {
        return NULL;
    }
    o->tag = LC31_TAG;
    o->state = 0;
    o->refs = 1;
    return o;
}

struct lc31_obj *lc31_retain(struct lc31_obj *o)
{
    if (o != NULL && o->tag == LC31_TAG) {
        o->refs += 1;
    }
    return o;
}

int lc31_step(struct lc31_obj *o, int delta)
{
    if (o == NULL || o->tag != LC31_TAG) {
        return -1;
    }
    o->state += delta;
    return o->state;
}

int lc31_release(struct lc31_obj *o)
{
    int remaining;
    if (o == NULL || o->tag != LC31_TAG) {
        return -1;
    }
    o->refs -= 1;
    remaining = o->refs;
    if (remaining == 0) {
        o->tag = 0u;
        free(o);
    }
    return remaining;
}

int main(void)
{
    struct lc31_obj *a = lc31_open();
    struct lc31_obj *b;
    int s1, s2, r1, r2;

    if (a == NULL) {
        printf("lc31 result: FAIL allocation\n");
        return 1;
    }
    b = lc31_retain(a); /* alias, same object */
    s1 = lc31_step(a, 5);
    s2 = lc31_step(b, 7);
    r1 = lc31_release(a);
    r2 = lc31_release(b); /* final release frees storage */
    if (b != a || s1 != 5 || s2 != 12 || r1 != 1 || r2 != 0) {
        printf("lc31 result: FAIL s1=%d s2=%d r1=%d r2=%d\n",
               s1, s2, r1, r2);
        return 1;
    }
    printf("lc31 result: OK s1=%d s2=%d r1=%d r2=%d\n", s1, s2, r1, r2);
    return 0;
}
