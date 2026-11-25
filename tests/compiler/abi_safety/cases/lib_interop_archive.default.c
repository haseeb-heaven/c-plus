#include <stdio.h>

int abi_lib_mul(int,int);
double abi_lib_scale(double);
struct LibPair { long a; long b; };
struct LibPair abi_lib_pair(long,long);
int main(void){
  struct LibPair p=abi_lib_pair(6,7);
  printf("lib=%d,%.1f,%ld,%ld\n", abi_lib_mul(6,7), abi_lib_scale(1.25), p.a, p.b);
  return 0;
}
