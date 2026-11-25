#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef CPLUS
#include "cplus/cplus.h"
#define SAFE_MEMCPY cplus_memcpy
#define SAFE_STRCPY cplus_strcpy
#else
#define SAFE_MEMCPY memcpy
#define SAFE_STRCPY(A,B,S) strcpy((A),(S))
#endif
#define N 2000000
static volatile char sink;
int main(void) {
  char src[64], dst[64];
  memset(src, 'x', sizeof(src) - 1);
  src[sizeof(src) - 1] = 0;
  for (int i = 0; i < N; i++) {
    SAFE_STRCPY(dst, sizeof(dst), src);
    SAFE_MEMCPY(dst + 1, dst, 32);
    sink = dst[7];
  }
  printf("%d\n", sink);
  return 0;
}
