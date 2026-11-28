#include <stddef.h>
struct pair { int a,b; };
int main(void){ struct pair p[3]={{1,2},{3,4},{5,6}}; int s=0;
  for(size_t i=0;i<3;i++) s+=p[i].a+p[i].b; return s==21?0:1; }
