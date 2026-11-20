/* Benchmark: create and destroy one resource per iteration.
 *
 * The C+ build gets its destructor call inserted by the compiler; the plain
 * build states it by hand at the same place.  Both do identical work, so the
 * result shows that automatic cleanup itself is free.
 */
#include <stdio.h>

#if defined(CPLUS)
resource struct Acc {
    int total;
};
#else
struct Acc {
    int total;
};
typedef struct Acc Acc;
#endif

static int destroyed;

void Acc_drop(Acc *self) {
    if (self->total == -1)
        destroyed++; /* never taken; keeps the call observable */
}

Acc Acc_make(int seed) {
    Acc a;
    a.total = seed;
    return a;
}

#define ITERATIONS 25000000

int main(void) {
    long long sum = 0;
    int i;

    for (i = 0; i < ITERATIONS; i++) {
        Acc a = Acc_make(i & 7);
        a.total += i & 3;
        sum += a.total;
#if !defined(CPLUS)
        Acc_drop(&a);
#endif
    }

    printf("raii_loop %lld %d\n", sum, destroyed);
    return 0;
}
