#include <stdio.h>
#include <stdint.h>

typedef int (*fnptr)(int);
int abi_probe_fn(int x){ return x; }
int main(void){
  fnptr f = abi_probe_fn;
  int *ip = 0;
  void *vp = 0;
  printf("ptr=%zu fnptr=%zu iptr=%zu uintptr=%zu int=%zu long=%zu llong=%zu\n",
         sizeof(void*), sizeof(fnptr), sizeof(int*), sizeof(uintptr_t),
         sizeof(int), sizeof(long), sizeof(long long));
  printf("null=%d\n", (f == abi_probe_fn) && (ip == 0) && (vp == 0));
  return 0;
}
