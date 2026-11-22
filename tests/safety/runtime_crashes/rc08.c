#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
struct Item { uint32_t tag; struct Item *next; };
int main(void){uint32_t target=0; printf("tag: "); if(scanf("%u",&target)!=1)return 1; struct Item*head=0; for(uint32_t i=1;i<=5;++i){struct Item*n=malloc(sizeof *n);n->tag=i;n->next=head;head=n;} for(struct Item*c=head;c;c=c->next){if(c->tag==target){free(c);memset(c,0xFF,sizeof *c);}} return 0;}
