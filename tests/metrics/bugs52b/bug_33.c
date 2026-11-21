#include <stddef.h>
void add_arrays(int*restrict dst,const int*restrict src,size_t n){ for(size_t i=0;i<n;i++) dst[i]+=src[i]; }
int main(void){ int data={1,2,3,4,5,6}; add_arrays(data+1,data,4); return data; }
