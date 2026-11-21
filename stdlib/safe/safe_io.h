#ifndef CPLUS_SAFE_IO_H
#define CPLUS_SAFE_IO_H
#include <stdio.h>
#include <stddef.h>
#if defined(__cplusplus)
extern "C" {
#endif
int cplus_fopen_s(FILE **out, const char *name, const char *mode);
int cplus_fclose_s(FILE **inout);
size_t cplus_fread_s(void *buf, size_t bmax, size_t size, size_t count, FILE *stream);
size_t cplus_fwrite_s(const void *buf, size_t bmax, size_t size, size_t count, FILE *stream);
#if defined(__cplusplus)
}
#endif
#endif
