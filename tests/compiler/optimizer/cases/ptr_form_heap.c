#include <stdio.h>
#include <stdlib.h>
int main(int argc, char **argv){
  int i, off;
  int *h;
  (void)argv;
  h = malloc(8*sizeof(int));
  if(!h) return 2;
  for(i=0;i<8;i++) h[i]=i+1;
  off = argc + 2;
  { int *p = h + off; printf("ph=%d\n", *p); }
  free(h);
  return 0;
}
