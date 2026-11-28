#include <stdio.h>
#include <stddef.h>

/* The reserved runtime ABI is declared by the shipped public header only.
 * STRICT deliberately rejects a user declaration of a reserved helper, so the
 * fixture includes the header instead of restating the prototype. */
#include <cplus/cplus.h>
int main(void){
  int a[4]; a[0]=1;a[1]=2;a[2]=3;a[3]=4;
  cplus_check_bounds(a,4,2,"abi_probe.c",__LINE__,"a[i]");
  printf("rlib=%d\n", a[2]);
  return 0;
}
