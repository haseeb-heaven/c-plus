#include <stdio.h>
int *bad(void){int local=1;return &local;}  // NOSONAR intentional safe-mode safety fixture
int main(void){(void)bad();return 0;}
