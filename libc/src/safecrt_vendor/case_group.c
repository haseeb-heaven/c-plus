/*------------------------------------------------------------------
 * case_group.c -- vendored safeclib strtolowercase_s / strtouppercase_s
 *
 * This translation unit includes, VERBATIM, the already-made MIT-licensed
 * upstream safeclib implementations of strtolowercase_s() and
 * strtouppercase_s(). Nothing is reimplemented here: the two upstream
 * translation units are #included below with only a symbol-prefix rename
 * applied, so their algorithms and constraint ordering are exactly the
 * pinned upstream code.
 *
 * ==================================================================
 * PROVENANCE (exact)
 * ==================================================================
 *   Upstream repository : rurban/safeclib <https://github.com/rurban/safeclib>
 *   Pinned commit       : 90b076e94c4b0fb202dddf948d5d620e5395c1b0
 *   License             : MIT (third_party/safeclib/COPYING)
 *
 *   Function 1: strtolowercase_s
 *     upstream path : third_party/safeclib/src/extstr/strtolowercase_s.c
 *     exported as   : cplus_v_strtolowercase_s / cplus_v__strtolowercase_s_chk
 *   Function 2: strtouppercase_s
 *     upstream path : third_party/safeclib/src/extstr/strtouppercase_s.c
 *     exported as   : cplus_v_strtouppercase_s / cplus_v__strtouppercase_s_chk
 *
 * The MIT copyright notices of both upstream files are reproduced verbatim
 * below. The only modifications to the included sources are the prefix-only
 * #defines immediately preceding the #include directives; no other source
 * line is changed. The prefixed symbols are additive to the checked runtime
 * ABI. The constraint-handler remap binds the upstream calls to the already
 * linked C+ handler (as documented in third_party/safeclib/PROVENANCE.md for
 * the timingsafe sources); it does not change any upstream algorithm.
 *------------------------------------------------------------------
 */

/* ======================= strtolowercase_s.c =======================
 * strtolowercase_s.c
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

/* ======================= strtouppercase_s.c =======================
 * strtouppercase.c
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
#define strtolowercase_s cplus_v_strtolowercase_s
#define _strtolowercase_s_chk cplus_v__strtolowercase_s_chk
#define strtouppercase_s cplus_v_strtouppercase_s
#define _strtouppercase_s_chk cplus_v__strtouppercase_s_chk

/* Bind the upstream constraint handler to the C+ safecrt handler already
 * exported by libcplus_safecrt.a; this is a name remap only. */
#define invoke_safe_str_constraint_handler                                     \
    cplus_sc_invoke_safe_str_constraint_handler

#include "extstr/strtolowercase_s.c"
#include "extstr/strtouppercase_s.c"
