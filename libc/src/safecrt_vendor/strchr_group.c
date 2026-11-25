/* Vendored rurban/safeclib strchr_s + strrchr_s, included verbatim.
 *
 * Provenance (record exactly):
 *   upstream : rurban/safeclib
 *   commit   : 90b076e94c4b0fb202dddf948d5d620e5395c1b0
 *   license  : MIT (third_party/safeclib/COPYING)
 *   sources  : third_party/safeclib/src/extstr/strchr_s.c
 *              third_party/safeclib/src/extstr/strrchr_s.c
 *
 * Copyright (c) 2017 by Reini Urban
 * All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use,
 * copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following
 * conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT.  IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * The upstream bodies below are NOT retyped: they are pulled in verbatim by
 * the preprocessor. Only names are prefixed/redirected:
 *   - the public/chk entry points become cplus_v_* (ABI additive);
 *   - the three internal helpers are mapped onto symbols that already exist
 *     in the checked runtime (libcplus_safecrt.a / libcplus_hardened.a), so
 *     no safeclib logic is duplicated here. The memrchr mapping drops the
 *     destbos argument because the vendored runtime helper is the 4-argument
 *     upstream port (cplus_sc_upstream_memrchr_s). */
#define strchr_s cplus_v_strchr_s
#define _strchr_s_chk cplus_v__strchr_s_chk_unbounded
#define strrchr_s cplus_v_strrchr_s
#define _strrchr_s_chk cplus_v__strrchr_s_chk

#define invoke_safe_str_constraint_handler                                     \
    cplus_sc_invoke_safe_str_constraint_handler
#define _strnlen_s_chk cplus_sc__strnlen_s_chk
#define _memrchr_s_chk(dest, dmax, ch, resultp, destbos)                       \
    cplus_sc_upstream_memrchr_s(dest, dmax, ch, resultp)

#include "extstr/strchr_s.c"
#include "extstr/strrchr_s.c"


#ifndef CPLUS_VENDOR_STRCHR_ESNULLP
#define CPLUS_VENDOR_STRCHR_ESNULLP 400
#define CPLUS_VENDOR_STRCHR_ESZEROL  401
#define CPLUS_VENDOR_STRCHR_ESLEMAX  403
#define CPLUS_VENDOR_STRCHR_ESNOTFND 409
#endif
/* Bounded entry point: the vendored worker scans with unbounded strchr()
 * before validating dmax, so a non-NUL-terminated object can over-read into a
 * guard page. Refuse it first using a bounded memchr over exactly dmax bytes.
 * No caller bound lie is required. */
int cplus_v__strchr_s_chk(const char *restrict dest, size_t dmax,
                          const int ch, char **restrict resultp,
                          const size_t destbos) {
    if (dest == (const char *)0 || resultp == (char **)0)
        return CPLUS_VENDOR_STRCHR_ESNULLP;
    if (dmax == 0u)
        return CPLUS_VENDOR_STRCHR_ESZEROL;
    if (destbos != 0u && dmax > destbos)
        return CPLUS_VENDOR_STRCHR_ESLEMAX;
    *resultp = (char *)0;
    if (memchr(dest, 0, dmax) == (const void *)0)
        return CPLUS_VENDOR_STRCHR_ESNOTFND; /* no in-bounds terminator */
    return cplus_v__strchr_s_chk_unbounded(dest, dmax, ch, resultp, destbos);
}
