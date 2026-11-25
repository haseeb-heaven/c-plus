#include <stddef.h>
static void qsort_i(int *a, int lo, int hi) { if(lo>=hi) return; int p=a[hi],i=lo;
  for(int j=lo;j<hi;j++) if(a[j]<p){int t=a[i];a[i]=a[j];a[j]=t;i++;}
  int t=a[i];a[i]=a[hi];a[hi]=t; qsort_i(a,lo,i-1); qsort_i(a,i+1,hi); }
int main(void){ int a[6]={5,2,9,1,7,3}; qsort_i(a,0,5);
  return a[0]==1 && a[5]==9 ? 0 : 1; }
