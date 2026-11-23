/*------------------------------------------------------------------
 * strstr_group.c -- vendored safeclib strstr_s / strcasestr_s
 *
 * This translation unit includes, VERBATIM, the already-made MIT-licensed
 * upstream safeclib implementations of strstr_s() and strcasestr_s().
 * Nothing is reimplemented here: the two upstream translation units are
 * #included below with only a symbol-prefix rename applied, so their
 * algorithms and constraint ordering are exactly the pinned upstream code.
 *
 * ==================================================================
 * PROVENANCE (exact)
 * ==================================================================
 *   Upstream repository : rurban/safeclib <https://github.com/rurban/safeclib>
 *   Pinned commit       : 90b076e94c4b0fb202dddf948d5d620e5395c1b0
 *   License             : MIT (third_party/safeclib/COPYING)
 *
 *   Function 1: strstr_s
 *     upstream path : third_party/safeclib/src/extstr/strstr_s.c
 *     exported as   : cplus_v_strstr_s / cplus_v__strstr_s_chk
 *   Function 2: strcasestr_s
 *     upstream path : third_party/safeclib/src/extstr/strcasestr_s.c
 *     exported as   : cplus_v_strcasestr_s / cplus_v__strcasestr_s_chk
 *
 * The MIT copyright notices of both upstream files are reproduced verbatim
 * below. The only modifications to the included sources are the four
 * prefix-only #defines immediately preceding the #include directives; no
 * other source line is changed. The prefixed symbols are additive to the
 * checked runtime ABI.
 *------------------------------------------------------------------
 */

/* ============================ strstr_s.c ============================
 * strstr_s.c
 *
 * November 2008, Bo Berry
 * September 2018, Reini Urban
 *
 * Copyright (c) 2008-2011 by Cisco Systems, Inc
 * Copyright (c) 2018 by Reini Urban
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
 * ==================================================================== */

/* ========================= strcasestr_s.c ===========================
 * strcasestr_s.c
 *
 * November 2008, Bo Berry
 *
 * Copyright (c) 2008-2011 by Cisco Systems, Inc
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
 * ==================================================================== */

/* Prefix-only symbol renames: the upstream bodies are included verbatim. */
#define strstr_s cplus_v_strstr_s
#define _strstr_s_chk cplus_v__strstr_s_chk
#define strcasestr_s cplus_v_strcasestr_s
#define _strcasestr_s_chk cplus_v__strcasestr_s_chk

/* Extra config macro (see docs/libc/vendor_STRSTR.md).
 *
 * Both upstream bodies call safeclib's constraint handler on a
 * runtime-constraint violation. scripts/build_safecrt.sh compiles safeclib
 * with this exact prefix, exporting the handler as
 * cplus_sc_invoke_safe_str_constraint_handler from libcplus_safecrt.a. This
 * translation unit is compiled outside that script, so the identical
 * prefix is applied here to resolve the reference at link time. The
 * handler's default action (ignore_handler_s) is a no-op, so constraint
 * violations simply return the documented error codes. */
#define invoke_safe_str_constraint_handler cplus_sc_invoke_safe_str_constraint_handler

#include "extstr/strstr_s.c"
#include "extstr/strcasestr_s.c"
