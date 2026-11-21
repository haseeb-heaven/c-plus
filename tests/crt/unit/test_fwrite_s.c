#include <assert.h>
#include <stdio.h>
#include <unistd.h>
#include "safe_io.h"
#include "safe_error.h"
int main(void) {
    const char *path="/tmp/cplus_unit_fwrite.tmp";
    FILE *f=NULL; assert(cplus_fopen_s(&f,path,"wb")==CPLUS_SAFE_OK);
    assert(cplus_fwrite_s("abc",3,1,3,f)==3);
    cplus_fclose_s(&f); unlink(path);
    puts("unit fwrite_s: ok");
    return 0;
}
