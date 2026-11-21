#include <stdlib.h>
int main(void){int*p=malloc(4);p=realloc(p,8);if(!p)return 1;free(p);return 0;}
