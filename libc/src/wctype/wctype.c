/* Platform-backed wide-character classification (C17 <wctype.h>, C+ libc slice).
 * Delegates to the installed host libc; logged per C+ convention with NULL guards. */
#include "../../log/log.h"
#include <errno.h>
#include <wctype.h>

int cplus_iswalpha(wint_t wc) { cplus_libc_log("iswalpha", "enter"); return iswalpha(wc) != 0; }
int cplus_iswdigit(wint_t wc) { cplus_libc_log("iswdigit", "enter"); return iswdigit(wc) != 0; }
int cplus_iswspace(wint_t wc) { cplus_libc_log("iswspace", "enter"); return iswspace(wc) != 0; }
int cplus_iswalnum(wint_t wc) { cplus_libc_log("iswalnum", "enter"); return iswalnum(wc) != 0; }
int cplus_iswupper(wint_t wc) { cplus_libc_log("iswupper", "enter"); return iswupper(wc) != 0; }
int cplus_iswlower(wint_t wc) { cplus_libc_log("iswlower", "enter"); return iswlower(wc) != 0; }
int cplus_iswxdigit(wint_t wc) { cplus_libc_log("iswxdigit", "enter"); return iswxdigit(wc) != 0; }
int cplus_iswprint(wint_t wc) { cplus_libc_log("iswprint", "enter"); return iswprint(wc) != 0; }
int cplus_iswgraph(wint_t wc) { cplus_libc_log("iswgraph", "enter"); return iswgraph(wc) != 0; }
int cplus_iswpunct(wint_t wc) { cplus_libc_log("iswpunct", "enter"); return iswpunct(wc) != 0; }
int cplus_iswcntrl(wint_t wc) { cplus_libc_log("iswcntrl", "enter"); return iswcntrl(wc) != 0; }
int cplus_iswblank(wint_t wc) { cplus_libc_log("iswblank", "enter"); return iswblank(wc) != 0; }
wint_t cplus_towupper(wint_t wc) { cplus_libc_log("towupper", "enter"); return towupper(wc); }
wint_t cplus_towlower(wint_t wc) { cplus_libc_log("towlower", "enter"); return towlower(wc); }

wctype_t cplus_wctype(const char *property) {
    cplus_libc_log("wctype", "enter");
    if (!property) { errno = EINVAL; return (wctype_t)0; }
    return wctype(property);
}

int cplus_iswctype(wint_t wc, wctype_t desc) {
    cplus_libc_log("iswctype", "enter");
    return iswctype(wc, desc) != 0;
}
