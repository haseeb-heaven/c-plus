/* Platform-backed stdlib extras (C+ libc slice), batch 6. Delegate to the
 * installed system libc; logged per C+ convention. Covers the POSIX temp-file
 * helpers (mkstemp/mkdtemp), realpath() and the BSD qsort_r() reentrant sort.
 *
 * qsort_r host signature (verified on macOS / BSD):
 *   void qsort_r(void *, size_t, size_t, void *,
 *                int (*)(void *, const void *, const void *));
 * (the context pointer precedes the comparator -- the BSD, not the glibc,
 * argument order). */
#define _GNU_SOURCE
#include "../../log/log.h"
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>   /* on macOS mkstemp()/mkdtemp() are declared here, not <stdlib.h> */

#if defined(__GLIBC__)
typedef struct cplus_qsort_r_ctx {
    int (*compar)(void *, const void *, const void *);
    void *thunk;
} cplus_qsort_r_ctx;

static int cplus_qsort_r_glibc_trampoline(const void *a, const void *b, void *arg) {
    cplus_qsort_r_ctx *ctx = (cplus_qsort_r_ctx *)arg;
    return ctx->compar(ctx->thunk, a, b);
}
#endif

int cplus_mkstemp(char *template) {
    cplus_libc_log("mkstemp", "enter");
    if (!template) { errno = EINVAL; return -1; }
    return mkstemp(template);
}
char *cplus_mkdtemp(char *template) {
    cplus_libc_log("mkdtemp", "enter");
    if (!template) { errno = EINVAL; return NULL; }
    return mkdtemp(template);
}
char *cplus_realpath(const char *path, char *resolved) {
    cplus_libc_log("realpath", "enter");
    if (!path) { errno = EINVAL; return NULL; }
    return realpath(path, resolved);
}
void cplus_qsort_r(void *base, size_t nmemb, size_t size, void *thunk,
                   int (*compar)(void *, const void *, const void *)) {
    cplus_libc_log("qsort_r", "enter");
    if (!base || !compar || size == 0 || nmemb <= 1) return;
#if defined(__APPLE__) || defined(__FreeBSD__) || defined(__NetBSD__) || defined(__OpenBSD__)
    /* BSD/macOS: qsort_r(base, nmemb, size, thunk, compar) */
    qsort_r(base, nmemb, size, thunk, compar);
#elif defined(__GLIBC__)
    /* glibc: qsort_r(base, nmemb, size, compar, thunk) — adapt via trampoline. */
    {
        cplus_qsort_r_ctx ctx;
        ctx.compar = compar;
        ctx.thunk = thunk;
        qsort_r(base, nmemb, size, cplus_qsort_r_glibc_trampoline, &ctx);
    }
#else
    /* Unknown qsort_r ABI: do NOT call it with a guessed argument order. */
    errno = ENOSYS;
#endif
}
