#include "../../log/log.h"
#include <stddef.h>
#include <stdint.h>

size_t cplus_strspn(const char *s, const char *accept){
    cplus_libc_log("strspn","enter");
    size_t i=0; for(;s && s[i];++i){ const char*a=accept; int ok=0; while(a && *a){ if(*a==s[i]){ok=1;break;} ++a;} if(!ok) break; } return i;
}
size_t cplus_strcspn(const char *s, const char *reject){
    cplus_libc_log("strcspn","enter");
    size_t i=0; for(;s && s[i];++i){ const char*r=reject; while(r && *r){ if(*r==s[i]) return i; ++r; } } return i;
}
char *cplus_strpbrk(const char *s, const char *accept){
    cplus_libc_log("strpbrk","enter");
    for(;s && *s;++s){ for(const char*a=accept; a&&*a; ++a) if(*a==*s) return (char*)(uintptr_t)s; } return NULL;
}
