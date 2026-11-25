#include <stdio.h>
#include <stdlib.h>
int main(void){
  int i;
  int *p;
  p = malloc(4*sizeof(int));
  if(!p) return 2;
  for(i=0;i<4;i++) p[i]=i+1;
  printf("ab=%d,%d\n", p[0], p[3]);
  free(p);
  printf("ab=clean\n");
  return 0;
}
