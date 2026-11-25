/* Cross-platform app 56: domain 5, fixed seed 56. */
#include <stdio.h>
#include <string.h>
#include <stddef.h>
static int hostiles(int n, int x) {
  if (n < 0 || n > 1000 || x < 0 || x > 1000) return -1;
  return n;
}
int main(void) {
  int seed = 56;
  int good = hostiles(seed % 100, seed % 10);
  int bad1 = hostiles((int)-1, 5);
  int bad2 = hostiles(5, 2000);
  printf("DATABASE_APP 56 %d %d %d\n", good, bad1, bad2);
  return (good >= 0 && bad1 == -1 && bad2 == -1) ? 0 : 1;
}
