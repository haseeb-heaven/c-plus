#include <stdlib.h>
struct node { int v; struct node *next; };
int main(void) { struct node *head=0; for (int i=0;i<5;i++) { struct node *n=malloc(sizeof *n);
  if(!n) return 2; n->v=i; n->next=head; head=n; } int sum=0;
  for (struct node *p=head;p;p=p->next) sum+=p->v;
  while (head) { struct node *t=head; head=head->next; free(t); } return sum==10 ? 0 : 1; }  // NOSONAR safe list teardown: head is advanced before free(t)
