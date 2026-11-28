#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <stddef.h>
#include <stddef.h>
#include <stdio.h>
extern int *__cplus_errno_location(void);
#define errno (*__cplus_errno_location())
void *cplus_malloc(size_t); void *cplus_calloc(size_t,size_t); void *cplus_realloc(void*,size_t); void cplus_free(void*);
void *cplus_memcpy(void*restrict,const void*restrict,size_t); void *cplus_memmove(void*,const void*,size_t); void *cplus_memset(void*,int,size_t); int cplus_memcmp(const void*,const void*,size_t);
size_t cplus_strlen(const char*); char *cplus_strcpy(char*restrict,const char*restrict); int cplus_strcmp(const char*,const char*);
int main(void){
    errno = 0;
    char *p = (char*)cplus_malloc(16);
    assert(p);
    cplus_memset(p, 'A', 15); p[15]=0;
    assert(cplus_strlen(p)==15);
    char buf[16];
    cplus_memcpy(buf,p,16);
    assert(cplus_memcmp(buf,p,16)==0);
    cplus_free(p);
    char d[16]; cplus_strcpy(d,"ok");
    assert(cplus_strcmp(d,"ok")==0);
    puts("libc_basic: ok");
    return 0;
}
