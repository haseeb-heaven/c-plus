#include <stdio.h>
int main(void) {
#if __STDC_VERSION__ >= 202000L
  bool ready = true; printf("ready=%d\n", (int)ready);
#else
  printf("pre-C23\n");
#endif
  return 0;
}
