#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
struct CacheNode { uint32_t id; uint64_t *heavy_blob; };
int main(void){struct CacheNode*slots=0; struct CacheNode*p=malloc(sizeof *p); if(!p)return 1; p->id=1; p->heavy_blob=calloc(1024,sizeof(uint64_t)); slots=p; slots=p; int ev=0,rd=1; printf("evict read: "); if(scanf("%d %d",&ev,&rd)!=2)return 1; if(ev>=0&&ev<4&&slots[ev]){free(slots[ev]->heavy_blob);free(slots[ev]);slots[ev]=0;} void*spray=calloc(1,sizeof(struct CacheNode)); if(rd>=0&&rd<4&&slots[rd]) slots[rd]->heavy_blob=99; free(spray); return 0;}
