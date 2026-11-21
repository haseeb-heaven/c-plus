/* C+ hardened libc: selective-link header. Does not replace host CRT unless
 * user explicitly links libcplus_hardened and uses these symbols. */
#ifndef CPLUS_LIBC_H
#define CPLUS_LIBC_H
#include <stddef.h>
#include <stdarg.h>
#include <stdio.h>
#ifdef __cplusplus
extern "C" {
#endif
void *cplus_malloc(size_t);
void *cplus_calloc(size_t,size_t);
void *cplus_realloc(void*,size_t);
void  cplus_free(void*);
void *cplus_memcpy(void*restrict,const void*restrict,size_t);
void *cplus_memmove(void*,const void*,size_t);
void *cplus_memset(void*,int,size_t);
int   cplus_memcmp(const void*,const void*,size_t);
size_t cplus_strlen(const char*);
char *cplus_strcpy(char*restrict,const char*restrict);
char *cplus_strncpy(char*restrict,const char*restrict,size_t);
char *cplus_strcat(char*restrict,const char*restrict);
char *cplus_strncat(char*restrict,const char*restrict,size_t);
int   cplus_strcmp(const char*,const char*);
int   cplus_strncmp(const char*,const char*,size_t);
int   cplus_isalpha(int); int cplus_isdigit(int); int cplus_isspace(int);
FILE *cplus_fopen(const char*,const char*); int cplus_fclose(FILE*);
size_t cplus_fread(void*,size_t,size_t,FILE*); size_t cplus_fwrite(const void*,size_t,size_t,FILE*);
int cplus_snprintf(char*,size_t,const char*,...); int cplus_vsnprintf(char*,size_t,const char*,va_list);
int cplus_printf(const char*,...); int cplus_fprintf(FILE*,const char*,...);
int cplus_scanf(const char*,...); int cplus_fscanf(FILE*,const char*,...);
#ifdef __cplusplus
}
#endif
#endif
