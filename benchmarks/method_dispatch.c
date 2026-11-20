/* Benchmark: many calls through a tiny value-type method.
 *
 * C+ lowers `p.add(x)` to `Counter_add(&p, x)`, exactly the call the plain C
 * build writes by hand.  Equal timings are the expected result; a large
 * difference would mean the lowering is doing something wrong.
 */
#include <stdio.h>

#if defined(CPLUS)
class Counter {
    int value;

    void add(int amount) {
        self->value += amount;
    }

    int get(void) {
        return self->value;
    }
};
#else
typedef struct Counter {
    int value;
} Counter;

static void Counter_add(Counter *self, int amount) {
    self->value += amount;
}

static int Counter_get(Counter *self) {
    return self->value;
}
#endif

#define ITERATIONS 50000000

int main(void) {
    Counter counter = {0};
    int i;

    for (i = 0; i < ITERATIONS; i++) {
#if defined(CPLUS)
        counter.add(i & 3);
#else
        Counter_add(&counter, i & 3);
#endif
    }

#if defined(CPLUS)
    printf("method_dispatch %d\n", counter.get());
#else
    printf("method_dispatch %d\n", Counter_get(&counter));
#endif
    return 0;
}
