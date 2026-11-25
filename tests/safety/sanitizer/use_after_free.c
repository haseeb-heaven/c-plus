#include <stdlib.h>
typedef void (*ReleaseFn)(void *);
int main(void) {
  int *p = malloc(sizeof(int) * 2);
  if (!p) return 2;
  ReleaseFn volatile release = (ReleaseFn)free;
  release(p);
  return *p;
}
