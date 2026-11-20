/* Benchmark: pointer-intensive traversal of a linked ring. */
#include <stdio.h>

#define NODES 1000000
#define ROUNDS 50

typedef struct Node {
    struct Node *next;
    int value;
} Node;

static Node nodes[NODES];

int main(void) {
    int i, r;
    Node *p;
    unsigned long long sum = 0;

    for (i = 0; i < NODES; i++) {
        nodes[i].next = &nodes[(i + 1) % NODES];
        nodes[i].value = i;
    }
    p = nodes;
    for (r = 0; r < ROUNDS; r++)
        for (i = 0; i < NODES; i++) {
            sum += p->value;
            p = p->next;
        }
    printf("chase %llu\n", sum);
    return 0;
}
