#include "cplus/cplus.h"
#include <stdio.h>
static void f(int *@notnull p) { (void)p; }
int main(void) { f(NULL); return 0; }
