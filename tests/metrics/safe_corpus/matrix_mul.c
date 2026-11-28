#include <stddef.h>
int main(void) { int a[2][2] = {{1,2},{3,4}}, b[2][2] = {{5,6},{7,8}}, c[2][2] = {{0}};
  for (size_t i=0;i<2;i++) for (size_t j=0;j<2;j++) for (size_t k=0;k<2;k++) c[i][j]+=a[i][k]*b[k][j];
  return c[0][0]==19 && c[1][1]==50 ? 0 : 1; }
