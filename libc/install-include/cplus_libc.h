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
#include <signal.h>
#include <wctype.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Hardened allocation with live metadata tracking ------------------------- */
void *cplus_malloc(size_t);
void *cplus_calloc(size_t,size_t);
void *cplus_realloc(void*,size_t);
void  cplus_free(void*);
int   cplus_free_checked(void **inout);

/* Allocation metadata and safe length-query API (thread-safe). */
typedef struct cplus_alloc_meta {
    size_t byte_size;
    size_t elem_size;
    size_t elem_count;
    unsigned long long generation;
    int is_live;
} cplus_alloc_meta;

int    cplus_alloc_get_meta(const void *ptr, cplus_alloc_meta *out);
int    cplus_alloc_is_live(const void *ptr);
size_t cplus_alloc_size(const void *ptr);
size_t cplus_alloc_elem_size(const void *ptr);
size_t cplus_alloc_length(const void *ptr);
int    cplus_alloc_query_length(const void *ptr, size_t elem_size, size_t *out_count);
int    cplus_alloc_check_generation(const void *ptr, unsigned long long generation);
int    cplus_transfer_ownership(void **dst, void **src);
size_t cplus_alloc_live_count(void);
size_t cplus_alloc_live_bytes(void);
unsigned long cplus_alloc_double_free_count(void);
unsigned long cplus_alloc_invalid_free_count(void);
void   cplus_alloc_fail_after(long count);
void   cplus_alloc_fail_reset(void);
#define cplus_alloc_len(ptr) \
    ((ptr) != NULL ? (cplus_alloc_size(ptr) / sizeof(*(ptr))) : (size_t)0)

#ifdef CPLUS_MALLOC_TRACKING
#undef malloc
#undef calloc
#undef realloc
#undef free
#define malloc(sz)      cplus_malloc(sz)
#define calloc(n, sz)   cplus_calloc((n), (sz))
#define realloc(p, sz)  cplus_realloc((p), (sz))
#define free(p)         cplus_free(p)
#endif
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
size_t cplus_strspn(const char*,const char*); size_t cplus_strcspn(const char*,const char*); char *cplus_strpbrk(const char*,const char*);
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
void cplus_abort(void); void cplus_exit(int);
FILE *cplus_fopen(const char*,const char*); int cplus_fclose(FILE*);
size_t cplus_fread(void*,size_t,size_t,FILE*); size_t cplus_fwrite(const void*,size_t,size_t,FILE*);
#if defined(__GNUC__) || defined(__clang__)
#define CPLUS_LIBC_FMT_PRINTF(f, a) __attribute__((format(printf, f, a)))
#define CPLUS_LIBC_FMT_SCANF(f, a)  __attribute__((format(scanf, f, a)))
#else
#define CPLUS_LIBC_FMT_PRINTF(f, a)
#define CPLUS_LIBC_FMT_SCANF(f, a)
#endif
int cplus_snprintf(char*,size_t,const char*,...) CPLUS_LIBC_FMT_PRINTF(3, 4); int cplus_vsnprintf(char*,size_t,const char*,va_list) CPLUS_LIBC_FMT_PRINTF(3, 0);
int cplus_printf(const char*,...) CPLUS_LIBC_FMT_PRINTF(1, 2); int cplus_fprintf(FILE*,const char*,...) CPLUS_LIBC_FMT_PRINTF(2, 3);
int cplus_scanf(const char*,...) CPLUS_LIBC_FMT_SCANF(1, 2); int cplus_fscanf(FILE*,const char*,...) CPLUS_LIBC_FMT_SCANF(2, 3);
int cplus_sprintf(char*,const char*,...) CPLUS_LIBC_FMT_PRINTF(2, 3); int cplus_vsprintf(char*,const char*,va_list) CPLUS_LIBC_FMT_PRINTF(2, 0);
int cplus_sscanf(const char*,const char*,...) CPLUS_LIBC_FMT_SCANF(2, 3); int cplus_vsscanf(const char*,const char*,va_list) CPLUS_LIBC_FMT_SCANF(2, 0);
int cplus_vfprintf(FILE*,const char*,va_list) CPLUS_LIBC_FMT_PRINTF(2, 0); int cplus_vprintf(const char*,va_list) CPLUS_LIBC_FMT_PRINTF(1, 0); int cplus_vfscanf(FILE*,const char*,va_list) CPLUS_LIBC_FMT_SCANF(2, 0);
int cplus_fseek(FILE*,long,int); long cplus_ftell(FILE*); int cplus_fflush(FILE*);
int cplus_remove(const char*); int cplus_rename(const char*,const char*);
char *cplus_fgets(char*,int,FILE*); int cplus_fputs(const char*,FILE*);
int cplus_getc(FILE*); int cplus_putc(int,FILE*);
int cplus_feof(FILE*); int cplus_ferror(FILE*); void cplus_clearerr(FILE*);
int cplus_putchar(int); int cplus_getchar(void); int cplus_puts(const char*); void cplus_perror(const char*);
int cplus_strerror_r(int,char*,size_t);
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
/* POSIX signal-set / signal-control extras (platform-backed) */
int cplus_sigemptyset(sigset_t*); int cplus_sigfillset(sigset_t*); int cplus_sigaddset(sigset_t*,int); int cplus_sigdelset(sigset_t*,int); int cplus_sigismember(const sigset_t*,int);
int cplus_sigaction(int,const struct sigaction*,struct sigaction*); int cplus_sigprocmask(int,const sigset_t*,sigset_t*); int cplus_sigsuspend(const sigset_t*);
int cplus_kill(pid_t,int); int cplus_sigwait(const sigset_t*,int*);
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
/* wctype (platform-backed C17 <wctype.h>) */
int cplus_iswalpha(wint_t); int cplus_iswdigit(wint_t); int cplus_iswspace(wint_t); int cplus_iswalnum(wint_t);
int cplus_iswupper(wint_t); int cplus_iswlower(wint_t); int cplus_iswxdigit(wint_t); int cplus_iswprint(wint_t);
int cplus_iswgraph(wint_t); int cplus_iswpunct(wint_t); int cplus_iswcntrl(wint_t); int cplus_iswblank(wint_t);
wint_t cplus_towupper(wint_t); wint_t cplus_towlower(wint_t);
wctype_t cplus_wctype(const char*); int cplus_iswctype(wint_t, wctype_t);
/* reentrant time, stream char/rewind, inttypes & multibyte stdlib helpers */
struct tm *cplus_gmtime_r(const time_t*, struct tm*); struct tm *cplus_localtime_r(const time_t*, struct tm*);
char *cplus_asctime_r(const struct tm*, char*); char *cplus_ctime_r(const time_t*, char*);
void cplus_rewind(FILE*); int cplus_fgetc(FILE*); int cplus_fputc(int, FILE*);
intmax_t cplus_strtoimax(const char *restrict, char **restrict, int);
uintmax_t cplus_strtoumax(const char *restrict, char **restrict, int);
int cplus_mblen(const char*, size_t); int cplus_mbtowc(wchar_t *restrict, const char *restrict, size_t);
int cplus_wctomb(char*, wchar_t);
size_t cplus_mbstowcs(wchar_t *restrict, const char *restrict, size_t);
size_t cplus_wcstombs(char *restrict, const wchar_t *restrict, size_t);

/* ---- Extended ISO C17 surface -------------------------------------------
 * All functions below are PLATFORM_BACKED: namespaced cplus_* wrappers
 * that delegate to the installed host C runtime. They are NOT bundled
 * upstream implementations. Exact host backend, contract, test mapping
 * and safety limitation for each symbol: docs/libc/API_TEST_MATRIX.md.
 * Raw pointer validity / NUL-termination / format-string correctness remain
 * the caller's responsibility; these wrappers never make raw C pointers safe.
 * Apple Clang does not implement format(wprintf/wscanf,...) checking, so wide
 * format strings are caller-responsibility contracts, not statically checked.
 * ----------------------------------------------------------------------- */
/* fenv (ISO C17 extras) */
int cplus_feraiseexcept(int excepts);
int cplus_fegetexceptflag(fexcept_t *flagp, int excepts);
int cplus_fesetexceptflag(const fexcept_t *flagp, int excepts);
int cplus_feholdexcept(fenv_t *envp);
int cplus_feupdateenv(const fenv_t *envp);
/* time (ISO C17 ASCII conversions) */
char *cplus_asctime(const struct tm *tm);
char *cplus_ctime(const time_t *timep);
/* wchar (ISO C17 numeric/restartable conversions) */
long cplus_wcstol(const wchar_t *nptr, wchar_t **endptr, int base);
long long cplus_wcstoll(const wchar_t *nptr, wchar_t **endptr, int base);
unsigned long cplus_wcstoul(const wchar_t *nptr, wchar_t **endptr, int base);
unsigned long long cplus_wcstoull(const wchar_t *nptr, wchar_t **endptr, int base);
double cplus_wcstod(const wchar_t *nptr, wchar_t **endptr);
float cplus_wcstof(const wchar_t *nptr, wchar_t **endptr);
long double cplus_wcstold(const wchar_t *nptr, wchar_t **endptr);
wchar_t *cplus_wcstok(wchar_t *s, const wchar_t *delim, wchar_t **saveptr);
size_t cplus_wcsftime(wchar_t *wcs, size_t maxsize, const wchar_t *format, const struct tm *tm);
/* wchar (ISO C17 multibyte + wide stream character I/O) */
size_t cplus_mbrtowc(wchar_t *pwc, const char *s, size_t n, mbstate_t *ps);
size_t cplus_wcrtomb(char *s, wchar_t wc, mbstate_t *ps);
size_t cplus_mbsrtowcs(wchar_t *dst, const char **src, size_t len, mbstate_t *ps);
size_t cplus_wcsrtombs(char *dst, const wchar_t **src, size_t len, mbstate_t *ps);
wint_t cplus_fputwc(wint_t wc, FILE *stream);
wint_t cplus_fgetwc(FILE *stream);
wint_t cplus_putwc(wint_t wc, FILE *stream);
wint_t cplus_getwc(FILE *stream);
wint_t cplus_putwchar(wint_t wc);
wint_t cplus_getwchar(void);
wint_t cplus_ungetwc(wint_t wc, FILE *stream);
/* wchar (ISO C17 wide formatted I/O) */
int cplus_wprintf(const wchar_t *fmt, ...);
int cplus_vwprintf(const wchar_t *fmt, va_list ap);
int cplus_fwprintf(FILE *stream, const wchar_t *fmt, ...);
int cplus_vfwprintf(FILE *stream, const wchar_t *fmt, va_list ap);
int cplus_swprintf(wchar_t *s, size_t n, const wchar_t *fmt, ...);
int cplus_vswprintf(wchar_t *s, size_t n, const wchar_t *fmt, va_list ap);
int cplus_wscanf(const wchar_t *fmt, ...);
int cplus_vwscanf(const wchar_t *fmt, va_list ap);
int cplus_fwscanf(FILE *stream, const wchar_t *fmt, ...);
int cplus_vfwscanf(FILE *stream, const wchar_t *fmt, va_list ap);
int cplus_swscanf(const wchar_t *str, const wchar_t *fmt, ...);
int cplus_vswscanf(const wchar_t *str, const wchar_t *fmt, va_list ap);
/* wctype (ISO C17 extensible mappings) */
wctrans_t cplus_wctrans(const char *property);
wint_t cplus_towctrans(wint_t wc, wctrans_t desc);
/* complex (ISO C17 full family) */
double complex cplus_csin(double complex z);
double complex cplus_ccos(double complex z);
double complex cplus_ctan(double complex z);
double complex cplus_csinh(double complex z);
double complex cplus_ccosh(double complex z);
double complex cplus_ctanh(double complex z);
double complex cplus_clog(double complex z);
double complex cplus_cpow(double complex x, double complex y);
double complex cplus_cproj(double complex z);
float cplus_cabsf(float complex z);
float cplus_cargf(float complex z);
float cplus_crealf(float complex z);
float cplus_cimagf(float complex z);
float complex cplus_conjf(float complex z);
float complex cplus_cexpf(float complex z);
float complex cplus_csqrtf(float complex z);
float complex cplus_csinf(float complex z);
float complex cplus_ccosf(float complex z);
float complex cplus_ctanf(float complex z);
float complex cplus_csinhf(float complex z);
float complex cplus_ccoshf(float complex z);
float complex cplus_ctanhf(float complex z);
float complex cplus_clogf(float complex z);
float complex cplus_cpowf(float complex x, float complex y);
float complex cplus_cprojf(float complex z);
long double cplus_cabsl(long double complex z);
long double cplus_cargl(long double complex z);
long double cplus_creall(long double complex z);
long double cplus_cimagl(long double complex z);
long double complex cplus_conjl(long double complex z);
long double complex cplus_cexpl(long double complex z);
long double complex cplus_csqrtl(long double complex z);
long double complex cplus_csinl(long double complex z);
long double complex cplus_ccosl(long double complex z);
long double complex cplus_ctanl(long double complex z);
long double complex cplus_csinhl(long double complex z);
long double complex cplus_ccoshl(long double complex z);
long double complex cplus_ctanhl(long double complex z);
long double complex cplus_clogl(long double complex z);
long double complex cplus_cpowl(long double complex x, long double complex y);
long double complex cplus_cprojl(long double complex z);
/* math (ISO C17 float family) */
float cplus_sinf(float x);
float cplus_cosf(float x);
float cplus_tanf(float x);
float cplus_asinf(float x);
float cplus_acosf(float x);
float cplus_atanf(float x);
float cplus_atan2f(float y,float x);
float cplus_sinhf(float x);
float cplus_coshf(float x);
float cplus_tanhf(float x);
float cplus_asinhf(float x);
float cplus_acoshf(float x);
float cplus_atanhf(float x);
float cplus_expf(float x);
float cplus_exp2f(float x);
float cplus_expm1f(float x);
float cplus_logf(float x);
float cplus_log2f(float x);
float cplus_log10f(float x);
float cplus_log1pf(float x);
float cplus_powf(float x,float y);
float cplus_sqrtf(float x);
float cplus_cbrtf(float x);
float cplus_hypotf(float x,float y);
float cplus_floorf(float x);
float cplus_ceilf(float x);
float cplus_truncf(float x);
float cplus_roundf(float x);
float cplus_nearbyintf(float x);
float cplus_rintf(float x);
long cplus_lrintf(float x);
long long cplus_llrintf(float x);
float cplus_lroundf(float x);
float cplus_llroundf(float x);
float cplus_fabsf(float x);
float cplus_fmodf(float x,float y);
float cplus_remainderf(float x,float y);
float cplus_fdimf(float x,float y);
float cplus_fminf(float x,float y);
float cplus_fmaxf(float x,float y);
float cplus_fmaf(float x,float y,float z);
float cplus_copysignf(float x,float y);
float cplus_ldexpf(float x,int e);
float cplus_scalbnf(float x,int n);
float cplus_scalblnf(float x,long n);
int cplus_ilogbf(float x);
float cplus_logbf(float x);
float cplus_erff(float x);
float cplus_erfcf(float x);
float cplus_tgammaf(float x);
float cplus_lgammaf(float x);
float cplus_nextafterf(float x,float y);
float cplus_nexttowardf(float x,long double y);
float cplus_nanf(const char *tagp);
float cplus_frexpf(float x,int *e);
float cplus_modff(float x,float *ip);
float cplus_remquof(float x,float y,int *quo);
/* math (ISO C17 long-double family) */
long double cplus_sinl(long double x);
long double cplus_cosl(long double x);
long double cplus_tanl(long double x);
long double cplus_asinl(long double x);
long double cplus_acosl(long double x);
long double cplus_atanl(long double x);
long double cplus_atan2l(long double x, long double y);
long double cplus_sinhl(long double x);
long double cplus_coshl(long double x);
long double cplus_tanhl(long double x);
long double cplus_asinhl(long double x);
long double cplus_acoshl(long double x);
long double cplus_atanhl(long double x);
long double cplus_expl(long double x);
long double cplus_exp2l(long double x);
long double cplus_expm1l(long double x);
long double cplus_logl(long double x);
long double cplus_log2l(long double x);
long double cplus_log10l(long double x);
long double cplus_log1pl(long double x);
long double cplus_powl(long double x, long double y);
long double cplus_sqrtl(long double x);
long double cplus_cbrtl(long double x);
long double cplus_hypotl(long double x, long double y);
long double cplus_floorl(long double x);
long double cplus_ceill(long double x);
long double cplus_truncl(long double x);
long double cplus_roundl(long double x);
long double cplus_nearbyintl(long double x);
long double cplus_rintl(long double x);
long cplus_lrintl(long double x);
long long cplus_llrintl(long double x);
long cplus_lroundl(long double x);
long long cplus_llroundl(long double x);
long double cplus_erfl(long double x);
long double cplus_erfcl(long double x);
long double cplus_tgammal(long double x);
long double cplus_lgammal(long double x);
long double cplus_fabsl(long double x);
long double cplus_fmodl(long double x, long double y);
long double cplus_remainderl(long double x, long double y);
long double cplus_remquol(long double x, long double y, int *quo);
long double cplus_fdiml(long double x, long double y);
long double cplus_fminl(long double x, long double y);
long double cplus_fmaxl(long double x, long double y);
long double cplus_fmal(long double x, long double y, long double z);
long double cplus_copysignl(long double x, long double y);
long double cplus_ldexpl(long double x, int n);
long double cplus_frexpl(long double x, int *exp);
long double cplus_modfl(long double x, long double *iptr);
long double cplus_scalbnl(long double x, int n);
long double cplus_scalblnl(long double x, long n);
int cplus_ilogbl(long double x);
long double cplus_logbl(long double x);
long double cplus_nextafterl(long double x, long double y);
long double cplus_nexttowardl(long double x, long double y);
long double cplus_nanl(const char *tagp);
/* stdlib (ISO C11 allocation/termination) */
void *cplus_aligned_alloc(size_t alignment, size_t size);
void cplus_quick_exit(int status);
int cplus_at_quick_exit(void (*func)(void));
void cplus__Exit(int status);
/* inttypes (ISO C17 wide intmax conversions) */
intmax_t cplus_wcstoimax(const wchar_t *restrict nptr, wchar_t **restrict endptr, int base);
uintmax_t cplus_wcstoumax(const wchar_t *restrict nptr, wchar_t **restrict endptr, int base);
#ifdef __cplusplus
}
#endif
#endif
