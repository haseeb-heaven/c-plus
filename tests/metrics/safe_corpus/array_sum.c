#include <stddef.h>
int main(void) { int a[8] = {1,2,3,4,5,6,7,8}; int s = 0;
  for (size_t i = 0; i < 8; i++) s += a[i]; return s == 36 ? 0 : 1; }
