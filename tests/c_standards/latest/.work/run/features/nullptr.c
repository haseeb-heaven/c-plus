#include <stdio.h>
int main(void) {
#if __STDC_VERSION__ >= 202000L
  int *p = nullptr; printf("pnull=%d\n", p == nullptr);
#else
  printf("pre-C23\n");
#endif
  return 0;
}
