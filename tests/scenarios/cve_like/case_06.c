/* Original user-supplied crash scenario 6 (minimally adjusted to compile). */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
struct CacheNode { uint32_t id; uint64_t *heavy_blob; };
int main(void){ struct CacheNode *slots[4]={0}; struct CacheNode *p=malloc(sizeof *p); if(!p) return 1;
 p->id=1; p->heavy_blob=calloc(1024,sizeof(uint64_t)); slots[0]=p; slots[1]=p;
 int ev=0,rd=1; printf("evict read: "); if(scanf("%d %d",&ev,&rd)!=2) return 1;
 if(ev>=0&&ev<4&&slots[ev]){ free(slots[ev]->heavy_blob); free(slots[ev]); slots[ev]=NULL; }
 void *spray=calloc(1,sizeof(struct CacheNode));
 if(rd>=0&&rd<4&&slots[rd]) slots[rd]->heavy_blob=(uint64_t*)99;
 free(spray); return 0; }
