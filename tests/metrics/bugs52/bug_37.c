#include <stdlib.h>
#include <string.h>
int main(void) { const char* src = "hello"; char* dst = (char*)malloc(strlen(src)); strcpy(dst, src); free(dst); return 0; }
