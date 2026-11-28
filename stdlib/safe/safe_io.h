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

/* Additive hardening query (no existing symbol/signature/layout is changed):
 * returns 1 when `stream` is recorded as closed by cplus_fclose_s and has not
 * since been re-registered by cplus_fopen_s, 0 otherwise (including NULL).
 * The checked format adapters use this to refuse use-after-close streams before
 * handing them to the host vfprintf/vfscanf, and the checked I/O adapters use it
 * to avoid fread/fwrite on a freed FILE*.  A stream that was never opened through
 * this layer is simply unreported (0), so raw-host streams keep their semantics. */
int cplus_io_stream_is_closed(const FILE *stream);
int cplus_safe_stream_was_closed(const FILE *stream);
size_t cplus_safe_open_stream_count(void);

char *cplus_fgets_s(char *buf, size_t bmax, int n, FILE *stream);
char *cplus_fgets_s_checked(char *buf, size_t known_bmax, int n, FILE *stream);
size_t cplus_fread_s_checked(void *buf, size_t known_bmax, size_t bmax, size_t size, size_t count, FILE *stream);
size_t cplus_fwrite_s_checked(const void *buf, size_t known_bmax, size_t bmax, size_t size, size_t count, FILE *stream);
int cplus_getenv_s(size_t *len, char *value, size_t valuesz, const char *name);

#ifndef CPLUS_SAFE_OBJ_SIZE
#if defined(__GNUC__) || defined(__clang__)
/* Type 1 = nearest enclosing object (member extent), NOT the whole struct.
 * Must match safe_format.h: the first header included wins the #ifndef, and a
 * type-0 definition here silently downgrades the struct-member guard there. */
#define CPLUS_SAFE_OBJ_SIZE(p) __builtin_object_size((p), 1)
#else
#define CPLUS_SAFE_OBJ_SIZE(p) ((size_t)-1)
#endif
#endif

#ifndef CPLUS_SAFE_NO_UNPREFIXED_MACROS
#undef fopen_s
#define fopen_s cplus_fopen_s
#undef fclose_s
#define fclose_s cplus_fclose_s
#undef fgets_s
#define fgets_s(buf, n, stream) cplus_fgets_s_checked((buf), CPLUS_SAFE_OBJ_SIZE(buf), (n), (stream))
#undef fread_s
#define fread_s(buf, bmax, size, count, stream) cplus_fread_s_checked((buf), CPLUS_SAFE_OBJ_SIZE(buf), (bmax), (size), (count), (stream))
#undef fwrite_s
#define fwrite_s(buf, bmax, size, count, stream) cplus_fwrite_s_checked((buf), CPLUS_SAFE_OBJ_SIZE(buf), (bmax), (size), (count), (stream))
#undef getenv_s
#define getenv_s cplus_getenv_s
#endif
#if defined(__cplusplus)
}
#endif
#include "safe_format.h"
#endif
