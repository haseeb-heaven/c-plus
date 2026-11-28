/* Platform-backed wide-character wrappers (C+ libc slice). Delegate to the
 * installed system libc; logged per C+ convention. */
#define _XOPEN_SOURCE 700
#include "../../log/log.h"
#include <wchar.h>

size_t cplus_wcslen(const wchar_t *s){ cplus_libc_log("wcslen","enter"); return wcslen(s); }
wchar_t *cplus_wcscpy(wchar_t *d,const wchar_t *s){ cplus_libc_log("wcscpy","enter"); return wcscpy(d,s); }
wchar_t *cplus_wcsncpy(wchar_t *d,const wchar_t *s,size_t n){ cplus_libc_log("wcsncpy","enter"); return wcsncpy(d,s,n); }
int cplus_wcscmp(const wchar_t *a,const wchar_t *b){ cplus_libc_log("wcscmp","enter"); return wcscmp(a,b); }
int cplus_wcsncmp(const wchar_t *a,const wchar_t *b,size_t n){ cplus_libc_log("wcsncmp","enter"); return wcsncmp(a,b,n); }
wchar_t *cplus_wcscat(wchar_t *d,const wchar_t *s){ cplus_libc_log("wcscat","enter"); return wcscat(d,s); }
wchar_t *cplus_wcsstr(const wchar_t *h,const wchar_t *n){ cplus_libc_log("wcsstr","enter"); return wcsstr(h,n); }
wchar_t *cplus_wmemcpy(wchar_t *d,const wchar_t *s,size_t n){ cplus_libc_log("wmemcpy","enter"); return wmemcpy(d,s,n); }
wchar_t *cplus_wmemmove(wchar_t *d,const wchar_t *s,size_t n){ cplus_libc_log("wmemmove","enter"); return wmemmove(d,s,n); }
wchar_t *cplus_wmemset(wchar_t *d,wchar_t c,size_t n){ cplus_libc_log("wmemset","enter"); return wmemset(d,c,n); }
int cplus_wmemcmp(const wchar_t *a,const wchar_t *b,size_t n){ cplus_libc_log("wmemcmp","enter"); return wmemcmp(a,b,n); }
