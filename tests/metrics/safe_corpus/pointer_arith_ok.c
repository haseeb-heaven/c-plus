#include <stddef.h>
int main(void){ int a[5]={1,2,3,4,5}; int *p=a; int s=0;
  for(size_t i=0;i<5;i++) s += *(p+i); return s==15 ? 0 : 1; }
