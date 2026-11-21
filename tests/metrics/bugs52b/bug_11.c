#include <stdlib.h>
int main(void){ int*a=(int*)malloc(sizeof(int)); int*b=(int*)malloc(sizeof(int)); if(a<b){*a=1;} free(a); free(b); return 0; }
