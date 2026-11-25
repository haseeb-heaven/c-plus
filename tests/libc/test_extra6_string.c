/* Covers the platform-backed BSD string wrappers added in batch 6
 * (strlcpy / strlcat / strnstr, all declared by macOS <string.h>). */
#define _DEFAULT_SOURCE
#include <assert.h>
#include <string.h>

size_t cplus_strlcpy(char*,const char*,size_t);
size_t cplus_strlcat(char*,const char*,size_t);
char *cplus_strnstr(const char*,const char*,size_t);

int main(void){
    /* strlcpy returns strlen(src) and always NUL-terminates the result */
    { char b[16]="XXXXXXXXXXXXXXX"; assert(cplus_strlcpy(b,"hello",sizeof b)==5); assert(strcmp(b,"hello")==0); }
    { char b[4]; assert(cplus_strlcpy(b,"hello",sizeof b)==5); assert(strcmp(b,"hel")==0); }  /* truncated */
    { char b[1]; assert(cplus_strlcpy(b,"abc",sizeof b)==3); assert(b[0]=='\0'); }            /* size-1 safe */
    { char b[8]; assert(cplus_strlcpy(b,"",sizeof b)==0); assert(b[0]=='\0'); }               /* empty src */

    /* strlcat appends and returns the length it tried to build */
    { char b[16]="ab"; assert(cplus_strlcat(b,"cd",sizeof b)==4); assert(strcmp(b,"abcd")==0); }
    { char b[4]="ab"; assert(cplus_strlcat(b,"cdef",sizeof b)==6); assert(strcmp(b,"abc")==0); } /* truncated */
    { char b[16]=""; assert(cplus_strlcat(b,"xy",sizeof b)==2); assert(strcmp(b,"xy")==0); }
    { char b[8]="abc"; assert(cplus_strlcat(b,"",sizeof b)==3); assert(strcmp(b,"abc")==0); }

    /* strnstr searches only within the first len bytes */
    { const char *h="hello world"; assert(cplus_strnstr(h,"world",11)==h+6); }
    { const char *h="hello world"; assert(cplus_strnstr(h,"world",5)==NULL); }
    { const char *h="abcabc"; assert(cplus_strnstr(h,"bc",2)==NULL); }
    { const char *h="abcabc"; assert(cplus_strnstr(h,"abc",3)==h); }
    { const char *h="abc"; assert(cplus_strnstr(h,"",3)==h); }
    { const char *h="hello"; assert(cplus_strnstr(h,"lo",5)==h+3); }
    return 0;
}
