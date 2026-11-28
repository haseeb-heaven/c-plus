#include <stddef.h>
int main(void){ int g[3][4]={{0}}; int s=0;
  for(size_t i=0;i<3;i++) for(size_t j=0;j<4;j++){ g[i][j]=(int)(i+j); s+=g[i][j]; }
  return s==18 ? 0 : 1; }
