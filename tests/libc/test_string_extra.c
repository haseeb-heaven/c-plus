#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <stddef.h>
void *cplus_memchr(const void*,int,size_t);
char *cplus_strchr(const char*,int);
char *cplus_strrchr(const char*,int);
char *cplus_strstr(const char*,const char*);
size_t cplus_strnlen(const char*,size_t);
int main(void){
    const char *s="abcbc";
    assert(cplus_memchr(s,'c',5)==s+2);
    assert(cplus_memchr(s,'z',5)==NULL);
    assert(cplus_strchr(s,'c')==s+2);
    assert(cplus_strchr(s,'z')==NULL);
    assert(cplus_strrchr(s,'c')==s+4);
    assert(cplus_strstr(s,"bc")==s+1);
    assert(cplus_strstr(s,"zz")==NULL);
    assert(cplus_strnlen("abc",10)==3);
    assert(cplus_strnlen("abc",2)==2);
    puts("string_extra: ok");
    return 0;
}
