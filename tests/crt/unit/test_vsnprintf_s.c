#include <assert.h>
#include <stdarg.h>
#include <string.h>
#include <stdio.h>
#include "safe_format.h"
static int f(char *b,size_t n,const char *fmt,...) { va_list ap; va_start(ap,fmt); int r=cplus_vsnprintf_s(b,n,fmt,ap); va_end(ap); return r; }
int main(void){ char b[8]; assert(f(b,sizeof(b),"hi")==2); assert(strcmp(b,"hi")==0); puts("unit vsnprintf_s: ok"); return 0; }
