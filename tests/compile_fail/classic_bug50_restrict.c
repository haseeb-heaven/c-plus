/* Bug 50: overlapping restrict parameters. */
#include <stdio.h>
void add(int *restrict dst, const int *restrict src, int n) { for (int i=0;i<n;i++) dst[i]+=src[i]; }
int main(void) { int a[]={1,2,3,4}; add(a+1,a,3); printf("%d\n",a[0]); return 0; }
