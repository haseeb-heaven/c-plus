/* Platform-backed extensible wide-character mappings (C17 <wctype.h>, C+ libc slice).
 * Provides only the two cplus_* wrappers missing from wctype.c; there is no bundled
 * locale/transliteration implementation here. Each wrapper delegates verbatim to the
 * exact installed host libc backend (wctrans / towctrans) and logged per C+ convention.
 * Valid mapping descriptors are only those returned by cplus_wctrans; callers must not
 * fabricate descriptor values, and this file invents no new descriptor semantics. */

#include "../../log/log.h"
#include <errno.h>
#include <wctype.h>

wctrans_t cplus_wctrans(const char *property) {
    wctrans_t desc;
    cplus_libc_log("wctrans", "enter");
    if (!property) { errno = EINVAL; return (wctrans_t)0; }
    desc = wctrans(property);
    /* glibc leaves errno untouched for an unknown property; enforce the
     * checked-libc contract deterministically across hosts. */
    if (desc == (wctrans_t)0 && errno == 0)
        errno = EINVAL;
    return desc;
}

wint_t cplus_towctrans(wint_t wc, wctrans_t desc) {
    cplus_libc_log("towctrans", "enter");
    /* Host behavior is preserved; desc is expected to come from cplus_wctrans. */
    return towctrans(wc, desc);
}
