/* Independent modern-C port of the "bst" Goose sample workload.
 * C11, no Goose source copied. See proof/modern_c_ports/README.md. */
#include "common.h"

struct node { int v; struct node *l, *r; };
static struct node *insert(struct node *n, int v) {
    if (!n) { struct node *p = (struct node *)calloc(1, sizeof(*p)); p->v = v; return p; }
    if (v < n->v) n->l = insert(n->l, v); else n->r = insert(n->r, v);
    return n;
}
static void inorder(struct node *n) {
    if (!n) return; inorder(n->l); printf("bst %d\n", n->v); inorder(n->r);
}
int main(void) {
    struct node *root = 0;
    int v, vals[5] = {4, 2, 6, 1, 9};
    for (v = 0; v < 5; ++v) root = insert(root, vals[v]);
    inorder(root);
    CHECK("bst_root", root->v == 4 && root->l->v == 2);
    report("BST"); return fails;
}
