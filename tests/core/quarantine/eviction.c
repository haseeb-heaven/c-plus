#include "cplus/cplus.h"
#define N 1100
int main(void){
  int *stale = cplus_new(int);
  cplus_release(stale);
  for (int i=0;i<N;i++){ int *q=cplus_alloc(sizeof(int)); cplus_release(q); }
  int *fresh = cplus_alloc(sizeof(int));
  /* Route through an opaque parameter so the checker cannot prove the
     pointer is stale here; this exercises the runtime eviction path. */
  volatile void *opaque = stale;
  cplus_check_live((void *)opaque);
  (void)fresh;
  return 0;
}
