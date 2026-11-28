/* Paired benchmark: struct traversal with an opaque stride.
 *
 * The stride is opaque (volatile / black_box) so the compiler cannot unroll
 * the walk into a constant-folded sum.  This stresses pointer chasing and
 * field access on a hot loop with identical output in both languages. */
#include <stdio.h>

#define NODES 64
#define ITERATIONS 20000000LL

typedef struct Node {
    long long value;
    long long pad[7]; /* make each node one cache line */
} Node;

int main(void) {
    volatile int seed = 1;
    int stride = seed | 1; /* odd stride keeps the ring traversal full-cycle */
    static Node nodes[NODES];
    long long total = 0;
    long long i;
    int at = 0;

    for (i = 0; i < NODES; i++)
        nodes[i].value = i * 7;

    for (i = 0; i < ITERATIONS; i++) {
        total += nodes[at].value;
        at = (at + stride) % NODES;
    }

    printf("struct_sum %lld\n", total);
    return 0;
}
