/* Vendored safeclib string-set implementations for the C+ runtime.
 *
 * Provenance (recorded exactly):
 *   Upstream:  rurban/safeclib @ 90b076e94c4b0fb202dddf948d5d620e5395c1b0
 *   License:   MIT (third_party/safeclib/COPYING)
 *   Sources:   third_party/safeclib/src/extstr/strnset_s.c
 *              third_party/safeclib/src/extstr/strset_s.c
 *              third_party/safeclib/src/extstr/strnterminate_s.c
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
 * Per-file copyright notices reproduced verbatim from the upstream sources:
 *
 *   strnset_s.c       Copyright (c) 2017 by Reini Urban
 *   strset_s.c        Copyright (c) 2017 by Reini Urban
 *   strnterminate_s.c Copyright (c) 2011 by Cisco Systems, Inc
 *   (each with the same MIT permission/warranty text as reproduced above).
 *
 * The upstream translation units below are included verbatim: only symbol
 * names are changed (prefixed) via the preprocessor. No upstream logic is
 * reimplemented or retyped here. The exported _chk entry points are new,
 * additive cplus_v_* names; the ABI is unchanged for all existing consumers.
 */

/* Public names plus their BOS-aware _chk variants.  Safeclib spells the
 * public names as header macros, so the object files only export the _chk
 * entry points; the public names are prefixed here as well so every upstream
 * identifier is isolated to this translation unit. */
#define strnset_s cplus_v_strnset_s
#define _strnset_s_chk cplus_v__strnset_s_chk
#define strset_s cplus_v_strset_s
#define _strset_s_chk cplus_v__strset_s_chk
#define strnterminate_s cplus_v_strnterminate_s
#define _strnterminate_s_chk cplus_v__strnterminate_s_chk

/* Resolve the upstream constraint-handler calls to the already-prefixed
 * symbol exported by build/safe/libcplus_safecrt.a (built by
 * scripts/build_safecrt.sh). This is a prefix-only rename; the handler
 * implementation itself is the pinned vendored one in that archive. */
#define invoke_safe_str_constraint_handler                                     \
    cplus_sc_invoke_safe_str_constraint_handler

#include "extstr/strnset_s.c"
#include "extstr/strset_s.c"
#include "extstr/strnterminate_s.c"
