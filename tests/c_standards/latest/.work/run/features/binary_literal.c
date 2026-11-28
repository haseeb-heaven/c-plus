#include <stdio.h>
int main(void) {
#if __STDC_VERSION__ >= 202000L
  int mask = 0b1011; printf("mask=%d\n", mask);
#else
  printf("pre-C23\n");
#endif
  return 0;
}
