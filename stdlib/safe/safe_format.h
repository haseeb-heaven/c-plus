#ifndef CPLUS_SAFE_FORMAT_H
#define CPLUS_SAFE_FORMAT_H
#include <stddef.h>
#include <stdio.h>
#include <stdarg.h>
#if defined(__cplusplus)
extern "C" {
#endif
int cplus_snprintf_s(char *dest, size_t dmax, const char *fmt, ...);
int cplus_vsnprintf_s(char *dest, size_t dmax, const char *fmt, va_list ap);
int cplus_printf_s(const char *fmt, ...);
int cplus_fprintf_s(FILE *stream, const char *fmt, ...);
int cplus_scanf_s(const char *fmt, ...);
int cplus_fscanf_s(FILE *stream, const char *fmt, ...);
#if defined(__cplusplus)
}
#endif
#endif
