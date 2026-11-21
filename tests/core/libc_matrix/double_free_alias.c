#include <stdlib.h>
int main(void){int*p=malloc(4);int*q=p;free(p);free(q);return 0;}  // NOSONAR intentional unsafe fixture for the C+ safe-mode checker
