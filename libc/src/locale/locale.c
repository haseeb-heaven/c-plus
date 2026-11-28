#include "../../log/log.h"
#include <locale.h>
#include <stddef.h>

char *cplus_setlocale(int category, const char *locale){ cplus_libc_log("setlocale","enter"); return setlocale(category,locale); }
struct lconv *cplus_localeconv(void){ cplus_libc_log("localeconv","enter"); return localeconv(); }
