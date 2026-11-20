#include <stdlib.h>
#include <stdio.h>
struct Node { struct Node *next; int value; };
int main(void) {
    struct Node *next = NULL;
    struct Node *newchild = malloc(sizeof(*newchild));
    if (!newchild) return 1;
    next = newchild;
    if (next != NULL) {
        next->next = newchild;
        next->value = 7;
    }
    printf("%d\n", next->value);
    free(newchild);
    return 0;
}
