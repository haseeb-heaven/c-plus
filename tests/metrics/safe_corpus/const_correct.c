#include <stddef.h>
static int read_all(const int *a,size_t n){ int s=0; for(size_t i=0;i<n;i++) s+=a[i]; return s; }
int main(void){ int a[4]={1,2,3,4}; return read_all(a,4)==10?0:1; }
