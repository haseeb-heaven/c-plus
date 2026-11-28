#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <stdint.h>
int main(void){int*p=(int*)(uintptr_t)0xdeadbeef;return *p;}
