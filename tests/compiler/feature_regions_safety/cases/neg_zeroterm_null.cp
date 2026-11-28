#include "cplus/cplus.h"
#include <stdio.h>
static void f(char *@zeroterm s) { (void)s; }
int main(void) { f(NULL); return 0; }
