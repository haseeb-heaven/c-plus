#include <stdio.h>
int main(int argc, char **argv){
  int a[8];
  int i, idx;
  (void)argv;
  for(i=0;i<8;i++) a[i]=i+1;
  idx = argc + 2;
  printf("bs=%d,%d\n", idx, a[idx]);
  return 0;
}
