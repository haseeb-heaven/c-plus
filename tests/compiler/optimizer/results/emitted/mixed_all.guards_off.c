#line 1 "/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/tests/compiler/optimizer/cases/mixed_all.c"
#include <stdio.h>
#include <stdlib.h>
 struct Guard { int id; };
typedef struct Guard Guard;
#line 4 "/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/tests/compiler/optimizer/cases/mixed_all.c"

void Guard_drop(Guard *self){ if(self->id!=0){ printf("gd=%d\n", self->id); self->id=0; } }
Guard Guard_make(int id){ Guard g = {0}; g.id=id; return g; Guard_drop(&g); 
#line 9 "/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/tests/compiler/optimizer/cases/mixed_all.c"
}
int main(int argc, char **argv){
  int idx, v, x;
  int a[4] = {2,4,6,8};
  int *p, *h, *hp;
  (void)argv;
  idx = argc + 1;
  v = a[idx - 1];
  x = v * 3;
  p = a + (idx - 1);
  h = malloc(4*sizeof(int));
  if(!h) return 2;
  h[2] = x;
  hp = h + 2;
  { Guard g = Guard_make(42); (void)g; Guard_drop(&g); 
#line 23 "/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/tests/compiler/optimizer/cases/mixed_all.c"
}
  printf("mix=%d,%d,%d,%d\n", v, x, *p, *hp);
  free(h);
  return 0;
}