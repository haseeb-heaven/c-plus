#include <stdio.h>
#include <stdlib.h>
#include "cplus/cplus.h"
int main(void){ int *ptr = malloc(-1); print("ptr is ", *ptr); return 0; }
