#ifndef CPLUS_SAFE_IO_H
#define CPLUS_SAFE_IO_H
#include <stdio.h>
#include <stddef.h>
#if defined(__cplusplus)
extern "C" {
#endif
/* Standard C parameter list and return type with checked diagnostics and
 * bounded recent-close double-close protection. */
FILE *cplus_fopen_s(const char *name, const char *mode);
int cplus_fclose_s(FILE *stream);

/* Bounded buffer I/O (Annex K style bmax parameter retained to prevent overflow). */
size_t cplus_fread_s(void *buf, size_t bmax, size_t size, size_t count, FILE *stream);
size_t cplus_fwrite_s(const void *buf, size_t bmax, size_t size, size_t count, FILE *stream);

#ifndef CPLUS_SAFE_NO_UNPREFIXED_MACROS
#undef fopen_s
#define fopen_s cplus_fopen_s
#undef fclose_s
#define fclose_s cplus_fclose_s
#undef fread_s
#define fread_s cplus_fread_s
#undef fwrite_s
#define fwrite_s cplus_fwrite_s
#endif
#if defined(__cplusplus)
}
#endif
#endif
