
#include <limits.h>
int main(void){ volatile int x=INT_MAX; x+=1; volatile int y=INT_MIN; y-=1; return x+y; }
