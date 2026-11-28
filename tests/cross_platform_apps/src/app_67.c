/* Cross-platform app 67: domain 6, fixed seed 67. */
#include <stdio.h>
#include <string.h>
#include <stddef.h>
static int hostiles(int n, int x) {
  if (n < 0 || n > 1000 || x < 0 || x > 1000) return -1;
  return n;
}
int main(void) {
  int seed = 67;
  int good = hostiles(seed % 100, seed % 10);
  int bad1 = hostiles((int)-1, 5);
  int bad2 = hostiles(5, 2000);
  printf("AUDIO_APP 67 %d %d %d\n", good, bad1, bad2);
  return (good >= 0 && bad1 == -1 && bad2 == -1) ? 0 : 1;
}
