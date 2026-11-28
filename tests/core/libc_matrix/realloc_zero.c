#include <stdlib.h>
int main(void){int*p=malloc(4);p=realloc(p,0);free(p);return 0;}
