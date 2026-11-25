/*------------------------------------------------------------------
 * stp_group.c -- vendored safeclib stpcpy_s / stpncpy_s group
 *
 * This translation unit does NOT reimplement the two functions. It includes
 * the already-vendored upstream sources verbatim, changing only the exported
 * symbol names with preprocessor prefixes so they do not collide with the
 * existing libcplus_safecrt.a build (which does not compile extstr/stpcpy_s.c
 * or extstr/stpncpy_s.c).
 *
 * ==================================================================
 * PROVENANCE (exact, from third_party/safeclib/PROVENANCE.md)
 * ==================================================================
 *   Upstream repository : rurban/safeclib <https://github.com/rurban/safeclib>
 *   Pinned commit       : 90b076e94c4b0fb202dddf948d5d620e5395c1b0
 *   License             : MIT (third_party/safeclib/COPYING;
 *                         Copyright (C) 2012, 2013 Cisco Systems;
 *                         Copyright (C) 2017 Reini Urban)
 *
 *   Function 1: stpcpy_s
 *     upstream path : third_party/safeclib/src/extstr/stpcpy_s.c
 *     exported as   : cplus_v_stpcpy_s  (checked worker: cplus_v__stpcpy_s_chk)
 *   Function 2: stpncpy_s
 *     upstream path : third_party/safeclib/src/extstr/stpncpy_s.c
 *     exported as   : cplus_v_stpncpy_s (checked worker: cplus_v__stpncpy_s_chk)
 *
 * The upstream copyright notices are reproduced verbatim inside the included
 * files below. The only adaptations are the symbol renames in the #defines
 * immediately following; every upstream line of code is included unchanged.
 * The result is additive to the checked runtime ABI (two new symbols).
 *------------------------------------------------------------------
 */

/* Prefix the two public entry points and their checked workers. */
#define stpcpy_s cplus_v_stpcpy_s
#define _stpcpy_s_chk cplus_v__stpcpy_s_chk
#define stpncpy_s cplus_v_stpncpy_s
#define _stpncpy_s_chk cplus_v__stpncpy_s_chk

/* Bind the safeclib-internal helpers referenced by the verbatim upstream
 * bodies to the already-linked, cplus_sc_-prefixed copies in
 * build/safe/libcplus_safecrt.a. This is a name-only change: no upstream
 * algorithm is altered or reimplemented here. */
#define invoke_safe_str_constraint_handler                                     \
    cplus_sc_invoke_safe_str_constraint_handler
#define _strnlen_s_chk cplus_sc__strnlen_s_chk

#include "extstr/stpcpy_s.c"
#include "extstr/stpncpy_s.c"
