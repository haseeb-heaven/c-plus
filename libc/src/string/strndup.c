#include "../../log/log.h"
#include <stdlib.h>
#include <string.h>

char *cplus_strndup(const char *s, size_t n){
    extern void *cplus_malloc(size_t);
    cplus_libc_log("strndup","enter");
    if(!s) return NULL;
    size_t len = 0; while(len<n && s[len]) len++;
    char *d = (char*)cplus_malloc(len+1);
    if(!d) return NULL;
    memcpy(d,s,len); d[len]='\0';
    return d;
}
