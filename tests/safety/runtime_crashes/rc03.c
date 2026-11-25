#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <inttypes.h>
struct DynamicTable { size_t capacity; uint64_t *entries; };
int main(void){struct DynamicTable t={.capacity=16,.entries=calloc(16,sizeof(uint64_t))}; if(!t.entries)return 1; uint64_t nc=0; printf("cap: "); if(scanf("%" SCNu64,&nc)!=1)return 1; t.entries=realloc(t.entries,(size_t)nc*sizeof(uint64_t)); t.capacity=(size_t)nc; t.entries=0xDEADBEEFULL; free(t.entries); return 0;}
