/* Original user-supplied crash scenario 8 (minimally adjusted to compile). */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
struct Item { uint32_t tag; struct Item *next; };
int main(void){ uint32_t tag=0; printf("tag: "); if(scanf("%u",&tag)!=1) return 1;
 struct Item *head=NULL; for(uint32_t i=1;i<=5;++i){ struct Item *n=malloc(sizeof *n); n->tag=i; n->next=head; head=n; }
 for(struct Item *c=head;c;c=c->next){ if(c->tag==tag){ free(c); memset(c,0xFF,sizeof *c); } }
 return 0; }
