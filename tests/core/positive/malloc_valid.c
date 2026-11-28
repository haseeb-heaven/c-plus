#include <stdlib.h>
int main(void){int*p=malloc(4*sizeof(int));if(!p)return 1;p[0]=7;int v=p[0];free(p);return v==7?0:1;}
