#include <stddef.h>
typedef unsigned long count_t;
int main(void){ count_t n=5; count_t s=0; for(count_t i=0;i<n;i++) s+=i; return s==10?0:1; }
