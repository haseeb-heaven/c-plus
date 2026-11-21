#ifndef CPLUS_SAFE_ALLOC_H
#define CPLUS_SAFE_ALLOC_H
#include <stddef.h>
#include "safe_error.h"
#if defined(__cplusplus)
extern "C" {
#endif
/* Optional C+ extension. NOT a standard Microsoft/Apple CRT allocation API. */
int malloc_s(void **out, size_t bytes);
int calloc_s(void **out, size_t count, size_t size);
int realloc_s(void **inout, size_t bytes);
void free_s(void **inout);
#if defined(__cplusplus)
}
#endif
#endif
