/* Platform-backed stdlib extras (C+ libc slice). Delegate to installed libc. */
#define _DEFAULT_SOURCE
#include "../../log/log.h"
#include <errno.h>
#include <stdlib.h>
#include <inttypes.h>
#include <stdint.h>

int cplus_setenv(const char *n,const char *v,int o){ cplus_libc_log("setenv","enter"); return setenv(n,v,o); }
int cplus_unsetenv(const char *n){ cplus_libc_log("unsetenv","enter"); return unsetenv(n); }
int cplus_putenv(char *s){ cplus_libc_log("putenv","enter"); return putenv(s); }
int cplus_rand_r(unsigned int *seed){ cplus_libc_log("rand_r","enter"); return rand_r(seed); }
float cplus_strtof(const char *n,char **e){ cplus_libc_log("strtof","enter"); return strtof(n,e); }
long double cplus_strtold(const char *n,char **e){ cplus_libc_log("strtold","enter"); return strtold(n,e); }
intmax_t cplus_imaxabs(intmax_t j){ cplus_libc_log("imaxabs","enter"); return imaxabs(j); }
imaxdiv_t cplus_imaxdiv(intmax_t n,intmax_t d){ cplus_libc_log("imaxdiv","enter"); return imaxdiv(n,d); }

intmax_t cplus_strtoimax(const char *restrict nptr, char **restrict endptr, int base) {
    cplus_libc_log("strtoimax","enter");
    if (!nptr) { if (endptr) *endptr = NULL; errno = EINVAL; return 0; }
    return strtoimax(nptr, endptr, base);
}
uintmax_t cplus_strtoumax(const char *restrict nptr, char **restrict endptr, int base) {
    cplus_libc_log("strtoumax","enter");
    if (!nptr) { if (endptr) *endptr = NULL; errno = EINVAL; return 0; }
    return strtoumax(nptr, endptr, base);
}
int cplus_mblen(const char *s, size_t n) {
    cplus_libc_log("mblen","enter");
    return mblen(s, n);
}
int cplus_mbtowc(wchar_t *restrict pwc, const char *restrict s, size_t n) {
    cplus_libc_log("mbtowc","enter");
    return mbtowc(pwc, s, n);
}
int cplus_wctomb(char *s, wchar_t wc) {
    cplus_libc_log("wctomb","enter");
    return wctomb(s, wc);
}
size_t cplus_mbstowcs(wchar_t *restrict pwcs, const char *restrict s, size_t n) {
    cplus_libc_log("mbstowcs","enter");
    if (!s || (n > 0 && !pwcs)) { errno = EINVAL; return (size_t)-1; }
    return mbstowcs(pwcs, s, n);
}
size_t cplus_wcstombs(char *restrict s, const wchar_t *restrict pwcs, size_t n) {
    cplus_libc_log("wcstombs","enter");
    if (!pwcs || (n > 0 && !s)) { errno = EINVAL; return (size_t)-1; }
    return wcstombs(s, pwcs, n);
}
