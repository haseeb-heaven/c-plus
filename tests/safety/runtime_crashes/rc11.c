#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
struct Arena { uint8_t *base; size_t size, offset; };
static void *arena_push(struct Arena*a,size_t bytes){ if(a->offset+bytes>a->size)return 0; void*p=a->base+a->offset; a->offset+=bytes; return p; }
int main(void){struct Arena a={.base=malloc(4096),.size=4096,.offset=64}; if(!a.base)return 1; size_t req=0; printf("req: "); if(scanf("%zu",&req)!=1)return 1; uint8_t*chunk=arena_push(&a,req); if(chunk){uint64_t*next=arena_push(&a,sizeof(uint64_t)); if(next)*next=123;} free(a.base); return 0;}
