/* Original user-supplied crash scenario 1 (minimally adjusted to compile). */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <inttypes.h>
struct Batch { uint32_t item_count; uint8_t payload[]; };
static uint64_t read_u64(void){ uint64_t v=0; printf("count: "); if(scanf("%" SCNu64,&v)!=1) return 0; return v; }
int main(void){ uint64_t count=read_u64(); if(!count) return 1;
 uint32_t alloc_items=(uint32_t)count;
 struct Batch *b=malloc(sizeof(struct Batch)+(size_t)alloc_items*32); if(!b) return 1;
 b->item_count=alloc_items;
 for(uint64_t i=0;i<count;++i) memset(b->payload+i*32,0xAA,32);
 free(b); return 0; }
