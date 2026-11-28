#include <stdio.h>
#include <stdint.h>
#include <stddef.h>

/* Real-world style: ABI width probe used by a serialization layer. */
typedef void (*VoidFn)(void);
typedef int (*IntFn)(int);
static void rw_noop(void) { }
static int rw_identity(int x) { int y = x; return y; }
int main(void) {
  void *obj = 0;
  int *ip = 0;
  VoidFn vf = rw_noop;
  IntFn iff = rw_identity;
  uintptr_t up = 0;
  printf("ptr=%zu,obj=%zu,ip=%zu,fn=%zu,vfn=%zu,up=%zu,st=%zu\n",
         sizeof(void *), sizeof(obj), sizeof(ip), sizeof(iff), sizeof(vf),
         sizeof(uintptr_t), sizeof(size_t));
  printf("null=%d,call=%d\n",
         (obj == 0) && (ip == 0) && (up == 0), iff(7));
  return 0;
}
