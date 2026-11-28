#include <stdio.h>
#include <stddef.h>

void cplus_check_bounds(const void *base, size_t count, size_t index,
                        const char *file, int line, const char *expr);
int main(void){
  int a[4]; a[0]=1;a[1]=2;a[2]=3;a[3]=4;
  cplus_check_bounds(a,4,2,"abi_probe.c",__LINE__,"a[i]");
  printf("rlib=%d\n", a[2]);
  return 0;
}
