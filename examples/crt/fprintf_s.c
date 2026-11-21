#include <stdio.h>
#include "safe_format.h"
#include "safe_io.h"
#include "safe_error.h"
int main(void){ FILE *f=NULL; if(cplus_fopen_s(&f,"/tmp/cplus_fprintf.tmp","wb+")!=CPLUS_SAFE_OK) return 1; cplus_fprintf_s(f,"%s","ok"); cplus_fclose_s(&f); remove("/tmp/cplus_fprintf.tmp"); puts("fprintf_s: ok"); return 0; }
