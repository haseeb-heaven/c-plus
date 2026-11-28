/*
 * c23_upstream_small.c -- pinned-upstream-backed small C23 gaps.
 *
 * Companion to libc/src/c23_small_gaps.c (host-first + SKIP). This file asks
 * the different question: can the remaining gaps be CLOSED by importing a
 * pinned upstream body instead of skipping? See docs/libc/C23_SMALL_UPSTREAM.md
 * for the full evidence table, exact URLs/revisions and verification log.
 *
 * RESULT
 *   memset_explicit (<string.h>)  CLOSED -- LLVM libc llvmorg-20.1.1,
 *       libc/src/string/memset_explicit.cpp (Apache-2.0 WITH LLVM-exception),
 *       body copied verbatim except for the documented minimal adaptations.
 *   timespec_getres (<time.h>)    BLOCKED -- no source at either pin:
 *       musl v1.2.6 / master ship only src/time/timespec_get.c; LLVM libc
 *       llvmorg-20.1.1 (and main) have no timespec_getres source or build
 *       target. No algorithm invented.
 *   FE_TIESAWAY (<fenv.h>)        BLOCKED -- compiler-provided macro; this
 *       host's SDK does not define it, and LLVM libc's own fenv-macros.h at
 *       the pinned revision does not define it either. No numeric value
 *       fabricated. A local alias is published only if the host defines it.
 *
 * HOST PROBE (2026-11-23): /usr/bin/clang, Apple clang 16.0.0
 * (clang-1600.0.26.6), arm64-apple-darwin27.2.0,
 * -isysroot /Library/Developer/CommandLineTools/SDKs/MacOSX.sdk.
 *   memset_explicit -> "use of undeclared identifier"
 *   timespec_getres -> undeclared
 *   FE_TIESAWAY     -> undefined (<fenv.h> has FE_TONEAREST/FE_UPWARD/
 *                       FE_DOWNWARD/FE_TOWARDZERO only).
 */

/* Feature macros precede system headers: glibc declares C23 functions only
 * with its full feature set enabled. */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE 1
#endif
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE 1
#endif

#include "../log/log.h"
#include <errno.h>
#include <fenv.h>
#include <stddef.h>
#include <string.h>
#include <time.h>

/*
 * Capability macros are deliberately 0 for the two blocked entries so that
 * callers and tests have one authoritative description of this translation
 * unit. They are facts about the pinned upstream availability, not guesses
 * about a future host.
 */
#define CPLUS_C23_UPSTREAM_HAVE_TIMESPEC_GETRES 0
#if defined(FE_TIESAWAY)
#  define CPLUS_C23_UPSTREAM_HAVE_FE_TIESAWAY 1
#  define CPLUS_C23_UPSTREAM_FE_TIESAWAY FE_TIESAWAY
#else
#  define CPLUS_C23_UPSTREAM_HAVE_FE_TIESAWAY 0
#endif

/* cplus_memset lives in libc/src/string/memory.c; there is no installed
 * public header for the internal cplus_* symbols, so declare it locally
 * (the same pattern used by the existing libc tests). */
extern void *cplus_memset(void *s, int c, size_t n);

/* The upstream source is C++ and spells the attribute [[gnu::noinline]].
 * In a C translation unit GCC/Clang require __attribute__((noinline)) --
 * identical semantics, and the only spelling accepted by -std=c99. */
#define CPLUS_UPSTREAM_NOINLINE __attribute__((noinline))

/* ====================================================================== */
/* Vendored: LLVM Project, libc/src/string/memset_explicit.cpp, revision    */
/* llvmorg-20.1.1 (blob a3e4dad874397463cb5315679f99a312ac3a53fe).         */
/* Upstream license header reproduced verbatim below.                      */
/* ====================================================================== */
//===-- Implementation of memset_explicit ---------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

/* Adaptations from the upstream body (nothing else changed):
 *   1. memset_explicit -> cplus_memset_explicit (public cplus_* rename).
 *   2. [[gnu::noinline]] -> CPLUS_UPSTREAM_NOINLINE (C spelling, see above).
 *   3. inline_memset(dst, static_cast<uint8_t>(value), count) ->
 *      cplus_memset(dst, value, count). The upstream dependency closure
 *      (src/string/memory_utils/inline_memset.h -> arch-specific memset
 *      headers, src/string/memory_utils/utils.h, src/__support/macros/... )
 *      is NOT self-contained; cplus_memset is an existing cplus_* function
 *      and performs the same (unsigned char) narrowing of `value`.
 *   4. __asm__ instead of the C++-only `asm` keyword (GNU spelling).
 *   5. one opt-in cplus_libc_log entry marker, per C+ convention.
 * The upstream compiler-barrier statement and return value are unchanged. */
/* On glibc >= 2.38 (or with the explicit host opt-in) c23_small_gaps.c
 * provides the host-backed wrapper; only emit the vendored body elsewhere. */
#if !defined(__GLIBC__) && !defined(CPLUS_C23_HOST_HAS_MEMSET_EXPLICIT)
CPLUS_UPSTREAM_NOINLINE void *cplus_memset_explicit(void *dst, int value,
                                                    size_t count) {
    cplus_libc_log("memset_explicit", "enter");

    cplus_memset(dst, value, count);
    // avoid dead store elimination
    // The asm itself should also be sufficient to behave as a compiler barrier.
    __asm__("" : : "r"(dst) : "memory");
    return dst;
}
#endif /* host-backed provider absent */
/* ======================= end vendored LLVM body ======================== */

/* timespec_getres: intentionally absent. No pinned upstream source and no
 * host declaration; emitting a body here would be a hand-written
 * implementation. FE_TIESAWAY: intentionally absent as a value; the alias
 * above is published only when the host compiler actually defines it. */
