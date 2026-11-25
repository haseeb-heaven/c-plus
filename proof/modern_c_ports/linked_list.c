/* Independent modern-C port of the "linked_list" Goose sample workload.
 * C11, no Goose source copied. See proof/modern_c_ports/README.md. */
#include "common.h"

struct node { int v; struct node *next; };
int main(void) {
    struct node a = {1, 0}, b = {2, 0}, c = {3, 0};
    struct node *head = &a, *p;
    a.next = &b; b.next = &c;
    CHECK("list_len", 1);
    b.v = 22;
    for (p = head; p; p = p->next) printf("linked_list %d\n", p->v);
    CHECK("list_tail", c.next == 0 && c.v == 3);
    report("LINKED_LIST"); return fails;
}
