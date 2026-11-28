
#include <limits.h>
int main(void){ volatile int x=INT_MAX; x++; volatile int m=INT_MIN; volatile int n=-m; return x+n; }
