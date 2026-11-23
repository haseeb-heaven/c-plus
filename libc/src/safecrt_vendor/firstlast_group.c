/*------------------------------------------------------------------
 * firstlast_group.c -- vendored safeclib first/last char and diff helpers
 *
 * This translation unit includes, VERBATIM, the already-made MIT-licensed
 * upstream safeclib implementations of strfirstchar_s(), strlastchar_s(),
 * strfirstdiff_s() and strlastdiff_s(). Nothing is reimplemented here: the
 * four upstream translation units are #included below with only a symbol
 * prefix rename applied, so their algorithms and constraint ordering are
 * exactly the pinned upstream code.
 *
 * ==================================================================
 * PROVENANCE (exact)
 * ==================================================================
 *   Upstream repository : rurban/safeclib <https://github.com/rurban/safeclib>
 *   Pinned commit       : 90b076e94c4b0fb202dddf948d5d620e5395c1b0
 *   License             : MIT (third_party/safeclib/COPYING)
 *
 *   Function 1: strfirstchar_s
 *     upstream path : third_party/safeclib/src/extstr/strfirstchar_s.c
 *     exported as   : cplus_v__strfirstchar_s_chk
 *   Function 2: strlastchar_s
 *     upstream path : third_party/safeclib/src/extstr/strlastchar_s.c
 *     exported as   : cplus_v__strlastchar_s_chk
 *   Function 3: strfirstdiff_s
 *     upstream path : third_party/safeclib/src/extstr/strfirstdiff_s.c
 *     exported as   : cplus_v__strfirstdiff_s_chk
 *   Function 4: strlastdiff_s
 *     upstream path : third_party/safeclib/src/extstr/strlastdiff_s.c
 *     exported as   : cplus_v__strlastdiff_s_chk
 *
 * The MIT copyright notices of all four upstream files are reproduced
 * verbatim below. The only modifications to the included sources are the
 * prefix-only #defines immediately preceding the #include directives (plus
 * the link-time constraint-handler redirect documented below); no other
 * source line is changed. The prefixed symbols are additive to the checked
 * runtime ABI.
 *------------------------------------------------------------------
 */

/* ========================== strfirstchar_s.c =======================
 * strfirstchar_s.c
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

/* ========================== strlastchar_s.c ========================
 * strlastchar_s.c
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

/* ========================== strfirstdiff_s.c =======================
 * strfirstdiff_s.c
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

/* ========================== strlastdiff_s.c ========================
 * strlastdiff_s.c
 *
 * November 2008, Bo Berry
 *
 * Copyright (c) 2008-2011, 2013 by Cisco Systems, Inc
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

/* Prefix-only symbol renames: the upstream bodies are included verbatim.
 * The plain names are macro-defined for parity with the pinned upstream
 * headers; in the non-FOR_DOXYGEN build the emitted entry points are the
 * _chk functions below. */
#define strfirstchar_s cplus_v_strfirstchar_s
#define _strfirstchar_s_chk cplus_v__strfirstchar_s_chk
#define strlastchar_s cplus_v_strlastchar_s
#define _strlastchar_s_chk cplus_v__strlastchar_s_chk
#define strfirstdiff_s cplus_v_strfirstdiff_s
#define _strfirstdiff_s_chk cplus_v__strfirstdiff_s_chk
#define strlastdiff_s cplus_v_strlastdiff_s
#define _strlastdiff_s_chk cplus_v__strlastdiff_s_chk

/* Extra link-time redirect (not an algorithm change). All four upstream
 * bodies call safeclib's constraint handler on a runtime-constraint
 * violation. scripts/build_safecrt.sh compiles safeclib with this exact -D
 * prefix, exporting the handler as cplus_sc_invoke_safe_str_constraint_handler
 * from libcplus_safecrt.a. This translation unit is compiled outside that
 * script, so the identical prefix is applied here so the reference resolves.
 * The handler's default action (ignore_handler_s) is a no-op, so constraint
 * violations simply return the documented upstream error codes. */
#define invoke_safe_str_constraint_handler                                     \
    cplus_sc_invoke_safe_str_constraint_handler

#include "extstr/strfirstchar_s.c"
#include "extstr/strlastchar_s.c"
#include "extstr/strfirstdiff_s.c"
#include "extstr/strlastdiff_s.c"
