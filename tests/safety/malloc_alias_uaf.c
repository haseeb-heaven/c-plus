#include <stdlib.h>
int main(void){int*p=malloc(sizeof(int));int*q=p;free(p);*q=42;return 0;}  // NOSONAR intentional safe-mode safety fixture
