#include <stddef.h>
static int square(int x){ return x*x; }
int main(void){ int s=0; for(int i=0;i<4;i++) s+=square(i); return s==14?0:1; }
