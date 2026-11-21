#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include "safe_format.h"
static int fmt(char *b, size_t n, const char *f, ...) { va_list ap; va_start(ap,f); int r=cplus_vsnprintf_s(b,n,f,ap); va_end(ap); return r; }
int main(void){ char b[16]; if (fmt(b,sizeof(b),"%d",42)!=2) return 1; if(strcmp(b,"42")!=0) return 1; puts("vsnprintf_s: ok"); return 0; }
