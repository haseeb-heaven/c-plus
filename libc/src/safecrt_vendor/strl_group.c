/*------------------------------------------------------------------
 * strl_group.c -- verbatim rurban/safeclib strlcat/strlcpy, prefixed
 *
 * This translation unit does NOT reimplement strlcpy() or strlcat().
 * It #includes the already-vendored safeclib sources verbatim and changes
 * only the exported symbol names (prefix-only, additive ABI). See
 * docs/libc/vendor_STRL.md for the exact commands and results.
 *
 * PROVENANCE
 *   Upstream repository : rurban/safeclib
 *   Pinned commit       : 90b076e94c4b0fb202dddf948d5d620e5395c1b0
 *   License             : MIT (third_party/safeclib/COPYING)
 *   Included verbatim   : third_party/safeclib/src/extstr/strlcat.c
 *                         third_party/safeclib/src/extstr/strlcpy.c
 *
 * Copyright (C) 2012, 2013 Cisco Systems
 * Copyright (C) 2017 Reini Urban
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
 *------------------------------------------------------------------
 */

/* Prefix-only helper binding: scripts/build_safecrt.sh compiles safeclib with
 * -Dinvoke_safe_str_constraint_handler=cplus_sc_invoke_safe_str_constraint_handler,
 * so the archive exports the prefixed name. Bind the verbatim upstream call
 * sites to that already-checked symbol instead of adding a new handler. */
#define invoke_safe_str_constraint_handler                                     \
    cplus_sc_invoke_safe_str_constraint_handler

/* Load safeclib_private.h (and transitively the SDK <string.h>, which on
 * macOS defines strlcat/strlcpy as FORTIFY macros in <secure/_string.h>)
 * BEFORE establishing the prefix macros. The header is include-guarded, so
 * the verbatim upstream .c files below will not re-define the SDK macros
 * over ours. */
#include "safeclib_private.h"

/* Undo the platform FORTIFY macros so the prefix macros below are the
 * effective definitions at the upstream definition sites. */
#undef strlcat
#undef strlcpy
#undef _strlcat_chk
#undef _strlcpy_chk

/* Prefix-only renames. Upstream strlcat.c/strlcpy.c export no *_chk variant;
 * the two _chk macros are defined for symmetry but emit no symbol. */
#define strlcat cplus_v_strlcat
#define _strlcat_chk cplus_v__strlcat_chk
#define strlcpy cplus_v_strlcpy
#define _strlcpy_chk cplus_v__strlcpy_chk

/* ============================== BEGIN VERBATIM UPSTREAM INCLUSION ======= */
#include "extstr/strlcat.c"
#include "extstr/strlcpy.c"
/* =============================== END VERBATIM UPSTREAM INCLUSION ======== */
