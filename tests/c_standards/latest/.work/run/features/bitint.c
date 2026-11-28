#include <stdio.h>
int main(void) {
#if __STDC_VERSION__ >= 202000L
  _BitInt(9) b = 3; printf("b=%d\n", (int)b);
#else
  printf("pre-C23\n");
#endif
  return 0;
}
