#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
void *cplus_memrchr(const void*,int,size_t);
int cplus_strcasecmp(const char*,const char*);
int cplus_strncasecmp(const char*,const char*,size_t);
char *cplus_strdup(const char*);
int main(void){
    const char *s="ababa"; assert(cplus_memrchr(s,'a',5)==s+4);
    assert(cplus_strcasecmp("AbC","aBc")==0);
    assert(cplus_strncasecmp("abcd","abZZ",2)==0);
    char *d=cplus_strdup("hi"); assert(d && strcmp(d,"hi")==0); free(d);
    puts("string_extra2: ok"); return 0;
}
