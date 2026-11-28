#include <stdlib.h>
int main(void){ size_t cap=4,n=0; int *a=malloc(cap*sizeof *a); if(!a) return 2;
  for(int i=0;i<12;i++){ if(n==cap){cap*=2; void *t=realloc(a,cap*sizeof *a); if(!t){free(a);return 2;} a=t;} a[n++]=(int)i; }
  int sum=0; for(size_t i=0;i<n;i++) sum+=a[i]; free(a); return sum==66 ? 0 : 1; }
