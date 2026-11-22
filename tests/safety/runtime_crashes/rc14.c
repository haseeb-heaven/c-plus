#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
struct Ring { uint64_t *slots; size_t capacity, cursor; };
int main(void){struct Ring r={.slots=calloc(32,sizeof(uint64_t)),.capacity=32,.cursor=0}; if(!r.slots)return 1; size_t skip=0; printf("skip: "); if(scanf("%zu",&skip)!=1)return 1; r.cursor+=skip; if(r.cursor==r.capacity)r.cursor=0; printf("%llu\n",(unsigned long long)r.slots[r.cursor]); free(r.slots); return 0;}
