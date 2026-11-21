#ifndef CPLUS_LIBC_LOCALE_H
#define CPLUS_LIBC_LOCALE_H
#include <locale.h>
#ifdef __cplusplus
extern "C" {
#endif
char *setlocale(int, const char*);
struct lconv *localeconv(void);
#ifdef __cplusplus
}
#endif
#endif
