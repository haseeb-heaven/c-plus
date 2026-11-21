#include <assert.h>
#include <stdio.h>
#include <unistd.h>
#include "safe_format.h"
#include "safe_io.h"
#include "safe_error.h"
int main(void){ FILE*f=NULL; int v=0; assert(cplus_fopen_s(&f,"/tmp/cplus_unit_fscanf.tmp","wb+")==CPLUS_SAFE_OK); cplus_fprintf_s(f,"42"); rewind(f); assert(cplus_fscanf_s(f,"%d",&v)==1 && v==42); cplus_fclose_s(&f); unlink("/tmp/cplus_unit_fscanf.tmp"); puts("unit fscanf_s: ok"); return 0; }
