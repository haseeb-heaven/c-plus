#include <stddef.h>
int find(const int *a, size_t n, int x) { size_t lo=0,hi=n;
  while (lo<hi) { size_t m=lo+(hi-lo)/2; if(a[m]<x) lo=m+1; else hi=m; }
  return lo<n && a[lo]==x ? (int)lo : -1; }
int main(void){ int a[6]={1,3,5,7,9,11}; return find(a,6,7)==3 ? 0 : 1; }
