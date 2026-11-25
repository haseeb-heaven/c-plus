#include <stdlib.h>
int main(void){ int *p=malloc(4*sizeof *p); if(!p) return 2;
  void *t=realloc(p,8*sizeof *p); if(t) p=t; else { free(p); return 2; }
  for(int i=0;i<8;i++) p[i]=i; int s=0; for(int i=0;i<8;i++) s+=p[i]; free(p);
  return s==28 ? 0 : 1; }
