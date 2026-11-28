#include "cplus/cplus.h"
#include <stdio.h>
int main(void){
  region r(128) {
    int *p = cplus_region_alloc(r, 4 * sizeof(int));
    if (!p) return 1;
    p[0]=10; p[1]=20;
    printf("%d %d\n", p[0], p[1]);
  }
  return 0;
}
