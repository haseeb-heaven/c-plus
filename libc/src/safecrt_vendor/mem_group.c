/* Vendored safeclib memory-extension implementations for the C+ runtime.
 *
 * Provenance (recorded exactly):
 *   Upstream:  rurban/safeclib @ 90b076e94c4b0fb202dddf948d5d620e5395c1b0
 *   License:   MIT
 *   Source:    third_party/safeclib/src/extmem/memchr_s.c
 *              third_party/safeclib/src/extmem/memccpy_s.c
 *
 * Copyright notice reproduced verbatim from third_party/safeclib/COPYING:
 *
 *   Safe C Library
 *
 *   Copyright (C) 2012, 2013 Cisco Systems
 *   Copyright (C) 2017 Reini Urban
 *   All rights reserved.
 *
 *   Permission is hereby granted, free of charge, to any person
 *   obtaining a copy of this software and associated documentation
 *   files (the "Software"), to deal in the Software without
 *   restriction, including without limitation the rights to use,
 *   copy, modify, merge, publish, distribute, sublicense, and/or
 *   sell copies of the Software, and to permit persons to whom the
 *   Software is furnished to do so, subject to the following
 *   conditions:
 *
 *   The above copyright notice and this permission notice shall be
 *   included in all copies or substantial portions of the Software.
 *
 *   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 *   EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 *   OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 *   NONINFRINGEMENT.  IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 *   HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 *   WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 *   FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 *   OTHER DEALINGS IN THE SOFTWARE.
 *
 * The upstream translation units below are included verbatim: only the
 * symbol names are changed (prefixed) via the preprocessor.  The constraint
 * handler references are likewise prefixed so they resolve against the
 * already-checked build/safe/libcplus_safecrt.a.  No upstream logic is
 * reimplemented or retyped here.
 */

/* Public names: safeclib spells these as header macros, so the object files
 * only export the BOS-aware _chk entry points.  The thin shims at the bottom
 * of this file expose stable, additive public entry points that forward to
 * the verbatim upstream bodies with an unknown object size (BOS_UNKNOWN). */
#define memchr_s cplus_v_memchr_s
#define _memchr_s_chk cplus_v__memchr_s_chk
#define memccpy_s cplus_v_memccpy_s
#define _memccpy_s_chk cplus_v__memccpy_s_chk

/* Resolve the upstream constraint-handler calls to the prefixed symbols
 * already exported by libcplus_safecrt.a (built by scripts/build_safecrt.sh). */
#define invoke_safe_mem_constraint_handler                                     \
    cplus_sc_invoke_safe_mem_constraint_handler
#define invoke_safe_str_constraint_handler                                     \
    cplus_sc_invoke_safe_str_constraint_handler

#include "extmem/memchr_s.c"
#include "extmem/memccpy_s.c"

/* Thin forwarding shims -- these do NOT reimplement upstream logic.  They
 * mirror exactly what the safeclib public header macros do at BOS_UNKNOWN. */
errno_t cplus_v_memchr_s(const void *restrict dest, rsize_t dmax, int ch,
                         void **resultp) {
    return cplus_v__memchr_s_chk(dest, dmax, ch, resultp, BOS_UNKNOWN);
}

errno_t cplus_v_memccpy_s(void *restrict dest, rsize_t dmax,
                          const void *restrict src, int c, rsize_t n) {
    return cplus_v__memccpy_s_chk(dest, dmax, src, c, n, BOS_UNKNOWN,
                                  BOS_UNKNOWN);
}
