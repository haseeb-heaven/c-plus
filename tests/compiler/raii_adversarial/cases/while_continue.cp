#include <stdio.h>
#include "cplus/cplus.h"
static void *g_ptrs[32]; static int g_n = 0;
resource struct R { int id; int *p; };
void R_drop(R *r){ if (r->id) { printf("D%d\n", r->id); cplus_release(r->p); r->id = 0; } }
static R R_new(int id){ R r = { id, (int *)cplus_alloc(4) }; g_ptrs[g_n++] = r.p; return r; }

static int scenario(void){
  int i = 0;
  while (i < 3) {
    ++i;
    R x = R_new(i * 10);
    { R y = R_new(i * 10 + 1);
      if (i == 2) { printf("C\n"); continue; }
      printf("I%d\n", i);
    }
    printf("X%d\n", i);
  }
  printf("END\n"); return 0;
}

int main(void){
  int rc = scenario();
  int live = 0;
  for (int i = 0; i < g_n; ++i) {
    if (cplus_allocation_generation(g_ptrs[i]) != 0) ++live;
  }
  printf("rc=%d live=%d\n", rc, live);
  return 0;
}
