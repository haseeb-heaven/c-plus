#include "cplus/cplus.h"
#include <stdio.h>
static void printz(char *@zeroterm s) { printf("%s\n", s); }
int main(void){ char text[] = "ok"; printz(text); return 0; }
