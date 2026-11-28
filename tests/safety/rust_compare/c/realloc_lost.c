#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(void){int*p=malloc(8);if(!p)return 1;realloc(p,64);return 0;}
