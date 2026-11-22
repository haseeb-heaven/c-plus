/* Platform-backed stdlib extras (C+ libc slice), batch 6. Delegate to the
 * installed system libc; logged per C+ convention. Covers the POSIX temp-file
 * helpers (mkstemp/mkdtemp), realpath() and the BSD qsort_r() reentrant sort.
 *
 * qsort_r host signature (verified on macOS / BSD):
 *   void qsort_r(void *, size_t, size_t, void *,
 *                int (*)(void *, const void *, const void *));
 * (the context pointer precedes the comparator -- the BSD, not the glibc,
 * argument order). */
#define _DEFAULT_SOURCE
#include "../../log/log.h"
#include <stdlib.h>
#include <unistd.h>   /* on macOS mkstemp()/mkdtemp() are declared here, not <stdlib.h> */

int cplus_mkstemp(char *template) {
    cplus_libc_log("mkstemp", "enter");
    return mkstemp(template);
}
char *cplus_mkdtemp(char *template) {
    cplus_libc_log("mkdtemp", "enter");
    return mkdtemp(template);
}
char *cplus_realpath(const char *path, char *resolved) {
    cplus_libc_log("realpath", "enter");
    return realpath(path, resolved);
}
void cplus_qsort_r(void *base, size_t nmemb, size_t size, void *thunk,
                   int (*compar)(void *, const void *, const void *)) {
    cplus_libc_log("qsort_r", "enter");
#if defined(__APPLE__) || defined(__FreeBSD__) || defined(__NetBSD__) || defined(__OpenBSD__)
    /* BSD/macOS: qsort_r(base, nmemb, size, thunk, compar) */
    qsort_r(base, nmemb, size, thunk, compar);
#elif defined(__GLIBC__)
    /* glibc: qsort_r(base, nmemb, size, compar, thunk) — reversed order. */
    qsort_r(base, nmemb, size, compar, thunk);
#else
    /* Unknown qsort_r ABI: do NOT call it with a guessed argument order. */
    errno = ENOSYS;
#endif
}
