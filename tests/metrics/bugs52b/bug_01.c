#include <stdlib.h>
struct Node { int val; struct Node* next; };
int main(void) { struct Node* head=(struct Node*)malloc(sizeof(struct Node)); head->next=(struct Node*)malloc(sizeof(struct Node)); head->next->next=NULL; for (struct Node* curr=head; curr!=NULL; curr=curr->next) { free(curr); } return 0; }
