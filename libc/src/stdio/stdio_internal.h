/* Internal stream-registry interface shared by the C+ libc stdio sources.
 * Not installed: this is a private implementation detail of the bundled libc.
 *
 * The double-close guard in stdio.c keeps a ring of recently closed FILE*
 * addresses. Any function that can create or re-open a FILE* MUST call
 * cplus_stdio_register() on success, otherwise the host allocator may recycle
 * a closed address and cplus_fclose() will refuse to close the new stream,
 * leaking the descriptor. cplus_fopen(), cplus_tmpfile(), cplus_fdopen() and
 * cplus_freopen() all register their result. */
#ifndef CPLUS_STDIO_INTERNAL_H
#define CPLUS_STDIO_INTERNAL_H

#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Register a newly created/reopened stream: clear any stale "closed" marker
 * and add it to the live table. Safe to call for an already-live stream. */
void cplus_stdio_register(FILE *stream);

#ifdef __cplusplus
}
#endif

#endif /* CPLUS_STDIO_INTERNAL_H */
