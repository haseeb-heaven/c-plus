#include <errno.h>
#include <string.h>
#include <stdio.h>
int main(void){const char*a=strerror(ENOENT);const char*b=strerror(ENOMEM);printf("%s | %s\n",a,b);return 0;}
