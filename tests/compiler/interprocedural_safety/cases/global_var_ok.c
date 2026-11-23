#include <stdio.h>

int g;
static void setg(void){ g=5; }
int main(void){ setg(); printf("v=%d\n",g); return 0; }
