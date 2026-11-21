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
void *cplus_memchr(const void*,int,size_t);
size_t cplus_strlen(const char*);
size_t cplus_strnlen(const char*,size_t);
char *cplus_strcpy(char*restrict,const char*restrict);
char *cplus_strncpy(char*restrict,const char*restrict,size_t);
char *cplus_strcat(char*restrict,const char*restrict);
char *cplus_strncat(char*restrict,const char*restrict,size_t);
int   cplus_strcmp(const char*,const char*);
int   cplus_strncmp(const char*,const char*,size_t);
char *cplus_strchr(const char*,int);
char *cplus_strrchr(const char*,int);
char *cplus_strstr(const char*,const char*);
void *cplus_memrchr(const void*,int,size_t);
int cplus_strcasecmp(const char*,const char*);
int cplus_strncasecmp(const char*,const char*,size_t);
char *cplus_strdup(const char*);
char *cplus_strerror(int); char *cplus_strtok(char*,const char*); char *cplus_strndup(const char*,size_t);
int cplus_isalpha(int); int cplus_isdigit(int); int cplus_isspace(int); int cplus_isalnum(int); int cplus_isupper(int); int cplus_islower(int); int cplus_isxdigit(int); int cplus_isprint(int); int cplus_isgraph(int); int cplus_ispunct(int); int cplus_iscntrl(int); int cplus_isblank(int); int cplus_toupper(int); int cplus_tolower(int);
int cplus_abs(int); long cplus_labs(long); int cplus_atoi(const char*); long cplus_atol(const char*); long cplus_strtol(const char*,char**,int); unsigned long cplus_strtoul(const char*,char**,int);
void cplus_qsort(void*,size_t,size_t,int(*)(const void*,const void*)); void *cplus_bsearch(const void*,const void*,size_t,size_t,int(*)(const void*,const void*));
int cplus_rand(void); void cplus_srand(unsigned int); int cplus_atexit(void(*)(void)); char *cplus_getenv(const char*); int cplus_system(const char*);
FILE *cplus_fopen(const char*,const char*); int cplus_fclose(FILE*);
size_t cplus_fread(void*,size_t,size_t,FILE*); size_t cplus_fwrite(const void*,size_t,size_t,FILE*);
int cplus_snprintf(char*,size_t,const char*,...); int cplus_vsnprintf(char*,size_t,const char*,va_list);
int cplus_printf(const char*,...); int cplus_fprintf(FILE*,const char*,...);
int cplus_scanf(const char*,...); int cplus_fscanf(FILE*,const char*,...);
int cplus_sprintf(char*,const char*,...); int cplus_vsprintf(char*,const char*,va_list);
int cplus_sscanf(const char*,const char*,...); int cplus_vsscanf(const char*,const char*,va_list);
int cplus_vfprintf(FILE*,const char*,va_list); int cplus_vprintf(const char*,va_list); int cplus_vfscanf(FILE*,const char*,va_list);
int cplus_fseek(FILE*,long,int); long cplus_ftell(FILE*); int cplus_fflush(FILE*);
int cplus_remove(const char*); int cplus_rename(const char*,const char*);
char *cplus_fgets(char*,int,FILE*); int cplus_fputs(const char*,FILE*);
int cplus_getc(FILE*); int cplus_putc(int,FILE*);
int cplus_feof(FILE*); int cplus_ferror(FILE*); void cplus_clearerr(FILE*);
int cplus_putchar(int); int cplus_getchar(void); int cplus_puts(const char*); void cplus_perror(const char*);
int *__cplus_errno_location(void); void cplus_libc_log_set_enabled(int);
#ifdef __cplusplus
}
#endif
#endif
