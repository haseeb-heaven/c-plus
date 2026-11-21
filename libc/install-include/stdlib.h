#ifndef CPLUS_LIBC_STDLIB_H
#define CPLUS_LIBC_STDLIB_H
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
void *malloc(size_t size);
void *calloc(size_t nmemb, size_t size);
void *realloc(void *ptr, size_t size);
void free(void *ptr);
void abort(void);
#ifdef __cplusplus
}
#endif
#endif
