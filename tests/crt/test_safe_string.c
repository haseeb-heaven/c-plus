#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "safe_string.h"
int main(void){
    char d[16];
    assert(cplus_strcpy_s(d,sizeof(d),"hi")==0);
    assert(strcmp(d,"hi")==0);
    /* too-small dest: Intel safestringlib sets dest[0]='\0' on error */
    assert(cplus_strcpy_s(d,3,"toolong")!=0);
    assert(d[0]=='\0');
    assert(cplus_strcpy_s(d,sizeof(d),"hi")==0);
    assert(cplus_strcat_s(d,sizeof(d),"!")==0);
    assert(strcmp(d,"hi!")==0);
    assert(cplus_strncat_s(d,sizeof(d),"world",2)==0);
    assert(strcmp(d,"hi!wo")==0);
    assert(cplus_strncpy_s(d,sizeof(d),"abcdef",3)==0);
    assert(strcmp(d,"abc")==0);
    assert(cplus_strnlen_s("abcd",10)==4);
    assert(cplus_strnlen_s(NULL,10)==0);
    puts("test_safe_string: ok");
    return 0;
}
