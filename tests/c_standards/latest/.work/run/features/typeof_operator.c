#include <stdio.h>
int main(void) {
#if __STDC_VERSION__ >= 202000L
  int v = 7; typeof(v) t = 9; printf("t=%d\n", t);
#else
  printf("pre-C23\n");
#endif
  return 0;
}
