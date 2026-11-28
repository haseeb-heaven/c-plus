/* typedef: aliases for scalars, structs (including self-reference) and fn ptrs. */
#ifndef CPLUS_LEGACY_C
#error "compatibility fixture must be built in LEGACY C mode"
#endif
#include <stdio.h>

typedef unsigned long size_alias;
typedef struct Node Node;
struct Node { int value; Node *next; };
typedef int (*BinaryOp)(int, int);

static int addop(int a, int b) { return a + b; }

int main(void) {
    Node a, b;
    BinaryOp op = addop;
    size_alias sz = sizeof(Node);

    a.value = 1; a.next = &b;
    b.value = 2; b.next = 0;

    printf("%d %d %d\n", a.value, a.next->value, op(8, 9));
    printf("%d\n", (int)(sz >= 8));
    return 0;
}
