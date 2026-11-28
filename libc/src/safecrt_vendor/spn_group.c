/*------------------------------------------------------------------
 * spn_group.c -- vendored safeclib strspn_s / strcspn_s group
 *
 * This translation unit does NOT reimplement the two span helpers. It
 * includes the pinned upstream rurban/safeclib sources verbatim and applies
 * a symbol prefix so the result is additive to the checked C runtime.
 *
 * ==================================================================
 * PROVENANCE (exact)
 * ==================================================================
 *   Upstream repository : rurban/safeclib <https://github.com/rurban/safeclib>
 *   Pinned commit       : 90b076e94c4b0fb202dddf948d5d620e5395c1b0
 *   License             : MIT (third_party/safeclib/COPYING)
 *
 *   Functions (verbatim upstream inclusion, prefix-only changes):
 *     third_party/safeclib/src/extstr/strspn_s.c
 *     third_party/safeclib/src/extstr/strcspn_s.c
 *
 * The MIT copyright notices of both upstream files are reproduced verbatim
 * inside the included sources. The only local edits are the #define lines
 * below, which rename public and internal symbols so nothing collides with
 * the existing libcplus_safecrt.a archive. No algorithm, constraint check,
 * or error-code path is changed.
 *------------------------------------------------------------------
 */

/* Prefix-only symbol remapping. */
#define strspn_s cplus_v_strspn_s
#define _strspn_s_chk cplus_v__strspn_s_chk
#define strcspn_s cplus_v_strcspn_s
#define _strcspn_s_chk cplus_v__strcspn_s_chk

/* Route the upstream constraint-handler calls to the already-prefixed
 * handler exported by build/safe/libcplus_safecrt.a. This is the same
 * -D prefix the build script applies to safeclib itself; no behavior change. */
#define invoke_safe_str_constraint_handler                                     \
    cplus_sc_invoke_safe_str_constraint_handler
#define invoke_safe_mem_constraint_handler                                     \
    cplus_sc_invoke_safe_mem_constraint_handler

#include "extstr/strspn_s.c"
#include "extstr/strcspn_s.c"
