/* Test 6: including the C+ metadata/public headers does not change any
 * ABI-visible layout.
 *
 * The SAME source is compiled twice with /usr/bin/clang:
 *   plain:    clang ... abi_compat_header_neutrality.c
 *   with C+:  clang ... -include cplus_libc.h -include cplus_libc_safe.h \
 *                      -Ilibc/install-include abi_compat_header_neutrality.c
 * The two outputs must be byte-identical (verified with diff -u).  This covers
 * the allocation metadata side-table declarations and the plain-C safety macro
 * layer: neither may turn a normal C type into a fat/capability/tagged type.
 *
 * Output uses snprintf + write() on purpose: the safe header #defines printf,
 * fprintf, strcpy, etc.; using them would change what is being compared.
 */
#include <stddef.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>
#include <time.h>
#include <signal.h>
#include <setjmp.h>
#include <pthread.h>
#include <complex.h>
#include <sys/types.h>
#include <unistd.h>

#define LINE(T)                                                               \
    do {                                                                      \
        char buf[192];                                                        \
        int n = snprintf(buf, sizeof buf, "LAYOUT %-28s size=%llu align=%llu\n", \
                         #T, (unsigned long long)sizeof(T),                   \
                         (unsigned long long)__alignof__(T));                 \
        if (n > 0)                                                            \
            (void)write(1, buf, (size_t)n);                                   \
    } while (0)

#define FIELD(T, F)                                                           \
    do {                                                                      \
        char buf[192];                                                        \
        int n = snprintf(buf, sizeof buf, "OFFSET %-28s %llu\n",              \
                         #T "." #F,                                           \
                         (unsigned long long)offsetof(T, F));                 \
        if (n > 0)                                                            \
            (void)write(1, buf, (size_t)n);                                   \
    } while (0)

int main(void)
{
    /* fundamental / pointer types */
    LINE(char);
    LINE(short);
    LINE(int);
    LINE(long);
    LINE(long long);
    LINE(float);
    LINE(double);
    LINE(long double);
    LINE(wchar_t);
    LINE(size_t);
    LINE(ptrdiff_t);
    LINE(intptr_t);
    LINE(uintptr_t);
    LINE(void *);
    LINE(int (*)(int));
    LINE(va_list);

    /* C runtime / platform types */
    LINE(div_t);
    LINE(ldiv_t);
    LINE(lldiv_t);
    LINE(max_align_t);
    LINE(mbstate_t);
    LINE(fpos_t);
    LINE(time_t);
    LINE(struct tm);
    LINE(sigset_t);
    LINE(struct sigaction);
    LINE(jmp_buf);
    LINE(FILE *);
    LINE(pthread_t);
    LINE(pthread_mutex_t);
    LINE(double complex);

    FIELD(div_t, quot);
    FIELD(div_t, rem);
    FIELD(struct tm, tm_sec);
    FIELD(struct tm, tm_year);
    FIELD(struct tm, tm_gmtoff);
    FIELD(struct tm, tm_zone);

    return 0;
}
