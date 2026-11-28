#include <stdlib.h>
#include <string.h>
int main(void){ const char *s="abcdef"; size_t n=strlen(s); char *d=malloc(n+1); if(!d) return 2;
  memcpy(d,s,n+1); size_t k=0; while(d[k]) k++; free(d); return k==6 ? 0 : 1; }
