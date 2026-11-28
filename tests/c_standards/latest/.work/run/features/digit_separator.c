#include <stdio.h>
int main(void) {
#if __STDC_VERSION__ >= 202000L
  int n = 1'000; printf("n=%d\n", n);
#else
  printf("pre-C23\n");
#endif
  return 0;
}
