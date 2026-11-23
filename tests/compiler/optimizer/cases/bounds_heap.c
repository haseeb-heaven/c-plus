#include <stdio.h>
#include <stdlib.h>
int main(int argc, char **argv){
  int n, idx, i;
  int *h;
  (void)argv;
  n = argc + 4;
  h = malloc(8*sizeof(int));
  if(!h) return 2;
  for(i=0;i<n;i++) h[i]=i*i;
  idx = n - 2;
  printf("bh=%d,%d\n", idx, h[idx]);
  free(h);
  return 0;
}
