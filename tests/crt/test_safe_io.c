#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "safe_io.h"
#include "safe_error.h"
int main(void){
    const char *path="/tmp/cplus_safe_io_test.bin";
    FILE *f=NULL;
    assert(cplus_fopen_s(&f,path,"wb")==CPLUS_SAFE_OK);
    const char msg[]="hello";
    assert(cplus_fwrite_s(msg,sizeof(msg),1,sizeof(msg),f)==sizeof(msg));
    assert(cplus_fclose_s(&f)==CPLUS_SAFE_OK && f==NULL);
    assert(cplus_fopen_s(&f,path,"rb")==CPLUS_SAFE_OK);
    char buf[16]={0};
    assert(cplus_fread_s(buf,sizeof(buf),1,sizeof(msg),f)==sizeof(msg));
    assert(strcmp(buf,"hello")==0);
    assert(cplus_fclose_s(&f)==CPLUS_SAFE_OK);
    assert(cplus_fopen_s(&f,"/no/such/cplus/path", "rb")!=CPLUS_SAFE_OK);
    assert(f==NULL);
    unlink(path);
    puts("test_safe_io: ok");
    return 0;
}
