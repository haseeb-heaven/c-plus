#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct S{int*p;};
int main(void){struct S s;s.p=malloc(8);if(!s.p)return 1;free(s.p);return *s.p;}
