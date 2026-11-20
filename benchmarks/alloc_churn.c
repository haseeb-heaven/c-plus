/* Benchmark: allocate, link and free two small objects per round.
 *
 * The safe build uses the C+ managed allocator, which keeps a side table so
 * that double release and invalid release are detected.  The plain build uses
 * malloc/free.  The difference is the real price of that bookkeeping.
 */
#include <stdio.h>
#include <stdlib.h>
#include "cplus/cplus.h"

#define ROUNDS 3000000

typedef struct Node {
    int value;
    struct Node *next;
} Node;

/* Keep one allocation observable so the C compiler cannot replace the whole
 * loop with constant arithmetic and stack-allocated objects. */
static void *volatile escaped;
static volatile int runtime_seed = 7;

int main(void) {
    long long total = 0;
    int r;

    for (r = 0; r < ROUNDS; r++) {
#if defined(CPLUS)
        Node *a = cplus_new(Node);
        Node *b = cplus_new(Node);
        if (!a || !b)
            return 1;
        a->value = r + runtime_seed;
        b->value = r + 1;
        a->next = b;
        total += a->value + a->next->value;
        if (r + 1 == ROUNDS)
            escaped = a;
        cplus_release(b);
        cplus_release(a);
#else
        Node *a = malloc(sizeof(*a));
        Node *b = malloc(sizeof(*b));
        if (!a || !b)
            return 1;
        a->value = r + runtime_seed;
        b->value = r + 1;
        a->next = b;
        total += a->value + a->next->value;
        if (r + 1 == ROUNDS)
            escaped = a;
        free(b);
        free(a);
#endif
    }

    printf("alloc_churn %lld\n", total);
    return 0;
}
