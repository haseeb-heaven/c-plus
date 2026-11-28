#include <stdlib.h>
struct GraphNode { int refs; struct GraphNode* peer; };
void unref(struct GraphNode*n){ if(--n->refs==0){ if(n->peer) unref(n->peer); free(n);} }
int main(void){ struct GraphNode*a=(struct GraphNode*)malloc(sizeof(struct GraphNode)); struct GraphNode*b=(struct GraphNode*)malloc(sizeof(struct GraphNode)); a->refs=2;b->refs=2;a->peer=b;b->peer=a; unref(a); unref(b); return 0; }
