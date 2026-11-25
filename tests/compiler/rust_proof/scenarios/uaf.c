#include <stdlib.h>
int main(void){int*p=malloc(4);if(!p)return 1;free(p);return *p;}
