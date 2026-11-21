#include "cplus/cplus.h"
int main(void) { return cplus_memcpy(NULL, "x", 1) != NULL; }  // NOSONAR intentional null-pointer safety probe
