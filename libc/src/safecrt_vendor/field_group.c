/*------------------------------------------------------------------
 * field_group.c -- vendored safeclib field compare/copy + collation
 *
 * This translation unit includes, VERBATIM, the already-made MIT-licensed
 * upstream safeclib implementations of strcmpfld_s(), strcpyfld_s() and
 * strcoll_s(). Nothing is reimplemented here: the three upstream translation
 * units are #included below with only a symbol-prefix rename applied, so
 * their algorithms and constraint ordering are exactly the pinned upstream
 * code.
 *
 * ==================================================================
 * PROVENANCE (exact)
 * ==================================================================
 *   Upstream repository : rurban/safeclib <https://github.com/rurban/safeclib>
 *   Pinned commit       : 90b076e94c4b0fb202dddf948d5d620e5395c1b0
 *   License             : MIT (third_party/safeclib/COPYING)
 *
 *   Function 1: strcmpfld_s
 *     upstream path : third_party/safeclib/src/extstr/strcmpfld_s.c
 *     exported as   : cplus_v_strcmpfld_s / cplus_v__strcmpfld_s_chk
 *   Function 2: strcpyfld_s
 *     upstream path : third_party/safeclib/src/extstr/strcpyfld_s.c
 *     exported as   : cplus_v_strcpyfld_s / cplus_v__strcpyfld_s_chk
 *   Function 3: strcoll_s
 *     upstream path : third_party/safeclib/src/extstr/strcoll_s.c
 *     exported as   : cplus_v_strcoll_s / cplus_v__strcoll_s_chk
 *
 * The MIT copyright notices of all three upstream files are reproduced
 * verbatim below. The only modifications to the included sources are the
 * six prefix-only #defines immediately preceding the #include directives;
 * no other source line is changed. The prefixed symbols are additive to the
 * checked runtime ABI.
 *------------------------------------------------------------------
 */

/* =========================== strcmpfld_s.c =========================
 * strcmpfld_s.c
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

/* =========================== strcpyfld_s.c =========================
 * strcpyfld_s.c
 *
 * November 2008, Bo Berry
 * October 2017, Reini Urban
 *
 * Copyright (c) 2008-2011 by Cisco Systems, Inc
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
 * ==================================================================== */

/* ============================ strcoll_s.c ==========================
 * strcoll_s.c -- string locale-aware compare
 *
 * September 2017, Reini Urban
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
 * ==================================================================== */

/* Prefix-only symbol renames: the upstream bodies are included verbatim. */
#define strcmpfld_s cplus_v_strcmpfld_s
#define _strcmpfld_s_chk cplus_v__strcmpfld_s_chk
#define strcpyfld_s cplus_v_strcpyfld_s
#define _strcpyfld_s_chk cplus_v__strcpyfld_s_chk
#define strcoll_s cplus_v_strcoll_s
#define _strcoll_s_chk cplus_v__strcoll_s_chk

/* The upstream bodies also call two safeclib helpers that
 * scripts/build_safecrt.sh already exports under its own cplus_sc_* prefix in
 * build/safe/libcplus_safecrt.a. These are prefix-only renames too: they
 * redirect those two call sites to the existing prefixed definitions so this
 * translation unit links against the checked runtime. No upstream source line
 * is changed and no helper is reimplemented here. */
#define invoke_safe_str_constraint_handler                                     \
    cplus_sc_invoke_safe_str_constraint_handler
#define _strnlen_s_chk cplus_sc__strnlen_s_chk

#include "extstr/strcmpfld_s.c"
#include "extstr/strcpyfld_s.c"
#include "extstr/strcoll_s.c"
