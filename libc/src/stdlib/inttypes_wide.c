/* PLATFORM-BACKED wide intmax conversions (C+ libc slice).
 * These are namespaced cplus_* wrappers only; they do not interpose host symbols.
 * Exact host backends:
 *   cplus_wcstoimax -> host wcstoimax(const wchar_t *restrict, wchar_t **restrict, int)
 *   cplus_wcstoumax -> host wcstoumax(const wchar_t *restrict, wchar_t **restrict, int)
 * All parsing, base/endptr handling, errno/ERANGE signalling and return values come
 * from the installed platform libc; no parsing is reimplemented here and no memory
 * is allocated. NULL required input is a defined hardening extension for otherwise-
 * undefined inputs only; valid inputs keep exact host behavior.
 */
#define _DEFAULT_SOURCE
#include "../../log/log.h"
#include <errno.h>
#include <inttypes.h>
#include <stdint.h>
#include <wchar.h>

intmax_t cplus_wcstoimax(const wchar_t *restrict nptr, wchar_t **restrict endptr, int base) {
    cplus_libc_log("wcstoimax","enter");
    if (!nptr) { if (endptr) *endptr = NULL; errno = EINVAL; return 0; }
    return wcstoimax(nptr, endptr, base);
}

uintmax_t cplus_wcstoumax(const wchar_t *restrict nptr, wchar_t **restrict endptr, int base) {
    cplus_libc_log("wcstoumax","enter");
    if (!nptr) { if (endptr) *endptr = NULL; errno = EINVAL; return 0; }
    return wcstoumax(nptr, endptr, base);
}
