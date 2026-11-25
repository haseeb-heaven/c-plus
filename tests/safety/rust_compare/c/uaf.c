#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(void){int*p=malloc(4);if(!p)return 1;free(p);return *p;}
