#include "cplus/cplus.h"
int main(void) { char b[4]; return cplus_memmove(b, NULL, 2) != b; }  // NOSONAR intentional null-pointer safety probe
