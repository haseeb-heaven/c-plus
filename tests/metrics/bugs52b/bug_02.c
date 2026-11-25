#include <stdlib.h>
struct DNode { int val; struct DNode *prev,*next; };
void remove_node(struct DNode* target){ if(target->prev) target->prev->next=target->next; free(target); }
int main(void){ struct DNode*a=(struct DNode*)calloc(1,sizeof(struct DNode)); struct DNode*b=(struct DNode*)calloc(1,sizeof(struct DNode)); a->next=b;b->prev=a; remove_node(a); if(b->prev) b->prev->val=10; free(b); return 0; }
