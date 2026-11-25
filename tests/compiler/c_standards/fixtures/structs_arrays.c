/* Deterministic aggregate layout and access; valid in every C standard. */
#include <stdio.h>

struct Pair {
    int a;
    int b;
};

struct Wrap {
    char c;
    struct Pair p;
    int arr[3];
};

int main(void)
{
    struct Pair p;
    struct Wrap w;
    int values[5];
    int i;
    int total = 0;

    p.a = 7;
    p.b = 9;
    w.c = 'z';
    w.p = p;
    w.arr[0] = 3;
    w.arr[1] = 2;
    w.arr[2] = 1;
    for (i = 0; i < 5; ++i)
        values[i] = i + 1;
    for (i = 0; i < 5; ++i)
        total += values[i];

    printf("pair=%d,%d\n", p.a, p.b);
    printf("wrap=%c,%d,%d,%d,%d,%d\n", w.c, w.p.a, w.p.b,
           w.arr[0], w.arr[1], w.arr[2]);
    printf("total=%d\n", total);
    printf("sizes=%lu,%lu,%lu\n", (unsigned long)sizeof(struct Pair),
           (unsigned long)sizeof(struct Wrap), (unsigned long)sizeof(values));
    return 0;
}
