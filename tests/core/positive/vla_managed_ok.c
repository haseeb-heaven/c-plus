/* VLA compatibility: a C99 variable-length array must be lowered safely in
 * DEFAULT/STRICT instead of being rejected by policy. */
#include <stdio.h>
int main(int argc, char **argv) {
  int n = argc + 2;
  int vla[n];
  int i;
  for (i = 0; i < n; i++)
    vla[i] = i + 1;
  printf("%d %d %d\n", n, vla[0], vla[n - 1]);
  return 0;
}
