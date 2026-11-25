#include <stdlib.h>
int main(void){int*a=malloc(4);int*b=malloc(4);if(a<b)return 1;free(a);free(b);return 0;}
