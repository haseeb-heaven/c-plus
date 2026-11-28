#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include "../../log/log.h"

/* Same bound as the SafeCRT layer (stdlib/safe/safe_memory.h); lengths above
 * this are not credible for one object and are rejected before any scan. */
#ifndef RSIZE_MAX_MEM
#define RSIZE_MAX_MEM ((size_t)(SIZE_MAX >> 1))
#endif

void *cplus_memrchr(const void *s, int c, size_t n){
    cplus_libc_log("memrchr","enter");
    /* Guard (#3, AUDIT_memory_string.md): refuse NULL before any dereference;
     * the scan below reads p[i-1] on its first iteration. */
    if (!s) { errno = EINVAL; return NULL; }
    /* Guard (#3): with n larger than the real object the very first read
     * p[n-1] is out of bounds. Reject absurd lengths so an unvalidated
     * SIZE_MAX is never used as the downward scan bound. */
    if (n > RSIZE_MAX_MEM) { errno = EOVERFLOW; return NULL; }
    const unsigned char *p=(const unsigned char*)s;
    unsigned char uc=(unsigned char)c;
    for(size_t i=n; i>0; i--) if(p[i-1]==uc) return (void *)(uintptr_t)(p+i-1);
    return NULL;
}
int cplus_strcasecmp(const char *a, const char *b){
    cplus_libc_log("strcasecmp","enter");
    while(*a && *b){
        unsigned char ca=(unsigned char)*a;
        unsigned char cb=(unsigned char)*b;
        if(ca>='A'&&ca<='Z') ca=(unsigned char)(ca-'A'+'a');
        if(cb>='A'&&cb<='Z') cb=(unsigned char)(cb-'A'+'a');
        if(ca!=cb) return (int)ca-(int)cb;
        ++a; ++b;
    }
    return (unsigned char)*a - (unsigned char)*b;
}
int cplus_strncasecmp(const char *a, const char *b, size_t n){
    cplus_libc_log("strncasecmp","enter");
    for(size_t i=0;i<n;i++){
        unsigned char ca=(unsigned char)a[i];
        unsigned char cb=(unsigned char)b[i];
        if(ca>='A'&&ca<='Z') ca=(unsigned char)(ca-'A'+'a');
        if(cb>='A'&&cb<='Z') cb=(unsigned char)(cb-'A'+'a');
        if(ca!=cb) return (int)ca-(int)cb;
        if(ca==0) return 0;
    }
    return 0;
}
char *cplus_strdup(const char *s){
    extern void *cplus_malloc(size_t);
    cplus_libc_log("strdup","enter");
    if(!s) return NULL;
    size_t n=0; while(s[n]) n++;
    char *p=(char*)cplus_malloc(n+1);
    if(!p) return NULL;
    for(size_t i=0;i<=n;i++) p[i]=s[i];
    return p;
}
