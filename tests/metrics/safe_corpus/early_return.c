#include <stddef.h>
static int classify(int n){ if(n<0) return -1; if(n==0) return 0; return 1; }
int main(void){ return classify(-5)==-1 && classify(0)==0 && classify(5)==1 ? 0 : 1; }
