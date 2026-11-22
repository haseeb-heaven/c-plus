/* C+ hardened libc: selective-link header. Does not replace host CRT unless
 * user explicitly links libcplus_hardened and uses these symbols. */
#ifndef CPLUS_LIBC_H
#define CPLUS_LIBC_H
#include <stddef.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>
#include <inttypes.h>
#include <sys/types.h>
#include <fenv.h>
#include <complex.h>
#include <pthread.h>
#include <string.h>
#include <math.h>
#include <time.h>
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
char *cplus_strsignal(int); int cplus_strcoll(const char*,const char*); size_t cplus_strxfrm(char*,const char*,size_t);
int cplus_strncasecmp(const char*,const char*,size_t);
char *cplus_strdup(const char*);
char *cplus_strerror(int); char *cplus_strtok(char*,const char*); char *cplus_strndup(const char*,size_t);
void *cplus_memccpy(void*,const void*,int,size_t); char *cplus_stpcpy(char*,const char*); char *cplus_stpncpy(char*,const char*,size_t); char *cplus_strtok_r(char*,const char*,char**); char *cplus_strsep(char**,const char*); void *cplus_memmem(const void*,size_t,const void*,size_t);
void cplus_swab(const void*,void*,ssize_t);
/* BSD string extras (platform-backed) */
size_t cplus_strlcpy(char*,const char*,size_t); size_t cplus_strlcat(char*,const char*,size_t); char *cplus_strnstr(const char*,const char*,size_t);
div_t cplus_div(int,int); ldiv_t cplus_ldiv(long,long); lldiv_t cplus_lldiv(long long,long long); long long cplus_llabs(long long); long long cplus_atoll(const char*);
long long cplus_strtoll(const char*,char**,int); unsigned long long cplus_strtoull(const char*,char**,int); double cplus_strtod(const char*,char**); double cplus_atof(const char*); float cplus_strtof(const char*,char**); long double cplus_strtold(const char*,char**);
int cplus_setenv(const char*,const char*,int); int cplus_unsetenv(const char*); int cplus_putenv(char*); int cplus_rand_r(unsigned int*); intmax_t cplus_imaxabs(intmax_t); imaxdiv_t cplus_imaxdiv(intmax_t,intmax_t);
int cplus_strverscmp(const char*,const char*); char *cplus_strcasestr(const char*,const char*); char *cplus_strchrnul(const char*,int);
int cplus_isalpha(int); int cplus_isdigit(int); int cplus_isspace(int); int cplus_isalnum(int); int cplus_isupper(int); int cplus_islower(int); int cplus_isxdigit(int); int cplus_isprint(int); int cplus_isgraph(int); int cplus_ispunct(int); int cplus_iscntrl(int); int cplus_isblank(int); int cplus_isascii(int); int cplus_toascii(int); int cplus_toupper(int); int cplus_tolower(int);
int cplus_abs(int); long cplus_labs(long); int cplus_atoi(const char*); long cplus_atol(const char*); long cplus_strtol(const char*,char**,int); unsigned long cplus_strtoul(const char*,char**,int);
char *cplus_ecvt(double,int,int*,int*); char *cplus_fcvt(double,int,int*,int*); char *cplus_gcvt(double,int,char*); double cplus_drand48(void); double cplus_erand48(unsigned short[3]); unsigned short *cplus_seed48(unsigned short[3]); long cplus_lrand48(void);
void cplus_qsort(void*,size_t,size_t,int(*)(const void*,const void*)); void *cplus_bsearch(const void*,const void*,size_t,size_t,int(*)(const void*,const void*));
/* POSIX/BSD stdlib extras (platform-backed) */
int cplus_mkstemp(char*); char *cplus_mkdtemp(char*); char *cplus_realpath(const char*,char*); void cplus_qsort_r(void*,size_t,size_t,void*,int(*)(void*,const void*,const void*));
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
/* wide char (platform-backed) */
size_t cplus_wcslen(const wchar_t*); wchar_t *cplus_wcscpy(wchar_t*,const wchar_t*); wchar_t *cplus_wcsncpy(wchar_t*,const wchar_t*,size_t);
int cplus_wcscmp(const wchar_t*,const wchar_t*); int cplus_wcsncmp(const wchar_t*,const wchar_t*,size_t); wchar_t *cplus_wcscat(wchar_t*,const wchar_t*);
wchar_t *cplus_wcsstr(const wchar_t*,const wchar_t*); wchar_t *cplus_wmemcpy(wchar_t*,const wchar_t*,size_t); wchar_t *cplus_wmemmove(wchar_t*,const wchar_t*,size_t); wchar_t *cplus_wmemset(wchar_t*,wchar_t,size_t); int cplus_wmemcmp(const wchar_t*,const wchar_t*,size_t);
/* math/time/locale platform-backed */
double cplus_fabs(double); double cplus_sqrt(double); double cplus_sin(double); double cplus_cos(double); double cplus_tan(double);
double cplus_pow(double,double); double cplus_exp(double); double cplus_log(double); double cplus_log10(double);
double cplus_floor(double); double cplus_ceil(double); double cplus_fmod(double,double);
double cplus_atan2(double,double); double cplus_asin(double); double cplus_acos(double); double cplus_atan(double);
double cplus_sinh(double); double cplus_cosh(double); double cplus_tanh(double); double cplus_log2(double); double cplus_cbrt(double);
double cplus_hypot(double,double); double cplus_round(double); double cplus_trunc(double); double cplus_fmin(double,double); double cplus_fmax(double,double);
double cplus_copysign(double,double); double cplus_ldexp(double,int); double cplus_frexp(double,int*); double cplus_modf(double,double*);
double cplus_nan(const char*); double cplus_nextafter(double,double); double cplus_fdim(double,double); double cplus_remainder(double,double); double cplus_log1p(double); double cplus_expm1(double); double cplus_asinh(double); double cplus_acosh(double); double cplus_atanh(double); double cplus_erf(double); double cplus_erfc(double); double cplus_tgamma(double); double cplus_lgamma(double);
double cplus_fma(double,double,double); double cplus_remquo(double,double,int*); double cplus_logb(double); int cplus_ilogb(double); float cplus_nanf(const char*); double cplus_exp2(double); double cplus_nearbyint(double); long cplus_lrint(double); long long cplus_llrint(double); double cplus_scalbn(double,int);
long cplus_time(long*); double cplus_difftime(long,long); long cplus_clock(void);
struct tm; struct tm *cplus_gmtime(const long*); struct tm *cplus_localtime(const long*); long cplus_mktime(struct tm*);
size_t cplus_strftime(char*,size_t,const char*,const struct tm*);
/* POSIX/C11 time extras (platform-backed) */
int cplus_clock_gettime(clockid_t,struct timespec*); int cplus_nanosleep(const struct timespec*,struct timespec*); int cplus_timespec_get(struct timespec*,int);
char *cplus_setlocale(int,const char*); struct lconv; struct lconv *cplus_localeconv(void);
void (*cplus_signal(int, void(*)(int)))(int); int cplus_raise(int);
/* stdio position/stream extras (platform-backed) */
int cplus_setvbuf(FILE*,char*,int,size_t); void cplus_setbuf(FILE*,char*);
int cplus_fgetpos(FILE*,fpos_t*); int cplus_fsetpos(FILE*,const fpos_t*);
int cplus_ungetc(int,FILE*); int cplus_fileno(FILE*);
FILE *cplus_fdopen(int,const char*); FILE *cplus_freopen(const char*,const char*,FILE*); FILE *cplus_tmpfile(void);
/* threads (pthreads-backed: host toolchain lacks C11 <threads.h>) */
int cplus_pthread_create(pthread_t*,const pthread_attr_t*,void*(*)(void*),void*); int cplus_pthread_join(pthread_t,void**);
int cplus_pthread_mutex_init(pthread_mutex_t*,const pthread_mutexattr_t*); int cplus_pthread_mutex_lock(pthread_mutex_t*); int cplus_pthread_mutex_unlock(pthread_mutex_t*); int cplus_pthread_mutex_destroy(pthread_mutex_t*);
int cplus_pthread_cond_init(pthread_cond_t*,const pthread_condattr_t*); int cplus_pthread_cond_signal(pthread_cond_t*); int cplus_pthread_cond_broadcast(pthread_cond_t*); int cplus_pthread_cond_wait(pthread_cond_t*,pthread_mutex_t*); int cplus_pthread_cond_destroy(pthread_cond_t*);
/* fenv (platform-backed) */
int cplus_feclearexcept(int); int cplus_fetestexcept(int); int cplus_fegetround(void); int cplus_fesetround(int); int cplus_fegetenv(fenv_t*); int cplus_fesetenv(const fenv_t*);
/* complex (platform-backed; host <complex.h> has no cadd/cmul, so those are omitted) */
double cplus_cabs(double complex); double cplus_carg(double complex); double cplus_creal(double complex); double cplus_cimag(double complex);
double complex cplus_conj(double complex); double complex cplus_cexp(double complex); double complex cplus_csqrt(double complex);
#ifdef __cplusplus
}
#endif
#endif
