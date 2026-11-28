/* Regression tests for the Fil-C liveness-query technique ported into
 * stdlib/allocator.hp.
 *
 * UPSTREAM SOURCE (fetched, not paraphrased)
 *   Repo:   https://github.com/pizlonator/fil-c
 *   Branch: deluge
 *   Commit: a80e32804fd44d295deb1ce1b0098962022da0a3
 *   Files:  filc/include/stdfil.h, filc/src/runtime.c
 *   Raw:    https://raw.githubusercontent.com/pizlonator/fil-c/a80e32804fd44d295deb1ce1b0098962022da0a3/filc/include/stdfil.h
 *   License: BSD 2-Clause-style (Epic Games / Filip Pizlo copyright header in
 *            stdfil.h and runtime.c); runtime is PAS-LICENSE.txt (libpas).
 *   Function ported: `zhasvalidcap` ("Tells if the pointer has a capability
 *   and that capability is not free"), with `zvalinbounds` bounds semantics
 *   and the `zgc_free` free-flag/free-singleton state.
 *
 * COVERAGE
 *   fd_live_state        - live: hasvalidcap / cap_state / covers / cap_check
 *   fd_freed_state       - freed: capability gone, generation token invalid
 *   fd_recycled_state    - recycled address fails the captured generation
 *   fd_bounds_edges      - overflow-safe bounds edges on a live base
 *   fd_invalid_refusal   - untracked/NULL/invalid-free refusal, no host free
 *
 * SAFETY INVARIANT
 *   After a release the old address is used ONLY as an opaque value in
 *   metadata queries; it is never dereferenced and never passed to
 *   cplus_release / stdlib_release again (a genuine second release is fatal
 *   by design).  Untracked storage is never handed to a raw free path: the
 *   refusal is exercised through stdlib_realloc and stdlib_transfer_ownership,
 *   both of which fail from the side table before any deallocation.
 *
 * STRICT-SAFE PATTERNS
 *   Function-unique local prefixes (fd_); no variable division/modulo; no raw
 *   pointer arithmetic or integer->pointer casts; no pointer subscripts;
 *   __builtin_memcpy only via the header's identity helpers; bounded code.
 *
 * OUTPUT: "<tag> <1|0>" per check, final line "ALLOC_DIAG <checks> <failures>".
 */

#include <stdio.h>
#include <string.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/allocator.hp"

/* Deliberately untracked storage; never passed to any release/free path. */
static char fd_untracked_storage[16];

static int fd_checks = 0;
static int fd_failed = 0;

static void fd_check(const char *tag, int cond) {
    fd_checks++;
    if (!cond)
        fd_failed++;
    print(tag, cond ? 1 : 0);
}

/* ---- live allocation: queryable, non-free capability state --------------- */
static void fd_live_state(void) {
    void *fd_p = stdlib_alloc(32);
    unsigned long long fd_g;
    stdlib_alloc_id fd_id;

    fd_g = stdlib_alloc_generation(fd_p);
    fd_check("fd_live_alloc", fd_p != NULL && fd_g > 0);

    /* zhasvalidcap analogue: a live base has a valid, non-free state. */
    fd_check("fd_live_hasvalidcap", stdlib_alloc_hasvalidcap(fd_p) == 1);
    fd_check("fd_live_cap_state",
             stdlib_alloc_cap_state(fd_p) == STDLIB_ALLOC_CAP_LIVE &&
             stdlib_alloc_cap_state(NULL) == STDLIB_ALLOC_CAP_NONE &&
             stdlib_alloc_cap_state((const void *)fd_untracked_storage) ==
                 STDLIB_ALLOC_CAP_NONE);

    /* zvalinbounds analogue, expressed with (base, offset) not pointer math. */
    fd_check("fd_live_covers_in",
             stdlib_alloc_cap_covers(fd_p, 0, 32) == 1 &&
             stdlib_alloc_cap_covers(fd_p, 16, 16) == 1 &&
             stdlib_alloc_cap_covers(fd_p, 32, 0) == 1);

    /* Check-before-use protocol: captured generation still authorizes access. */
    fd_check("fd_live_identity",
             stdlib_alloc_identity(fd_p, &fd_id) == 0 &&
             fd_id.generation == fd_g && fd_id.byte_size == 32);
    fd_check("fd_live_cap_check", stdlib_alloc_cap_check(fd_p, fd_g, 8, 8) == 1);
    fd_check("fd_live_cap_check_zero_gen",
             stdlib_alloc_cap_check(fd_p, 0, 0, 1) == 0 &&
             stdlib_alloc_cap_check(NULL, fd_g, 0, 1) == 0);

    stdlib_release(fd_p);
}

/* ---- freed: the capability is gone, the generation token is invalid ------ */
static void fd_freed_state(void) {
    void *fd_p = stdlib_alloc(24);
    void *fd_old = fd_p; /* opaque value only after the release below */
    unsigned long long fd_g;
    stdlib_alloc_id fd_id;

    fd_g = stdlib_alloc_generation(fd_p);
    (void)stdlib_alloc_identity(fd_p, &fd_id);
    fd_check("fd_freed_before",
             fd_p != NULL && fd_g > 0 &&
             stdlib_alloc_hasvalidcap(fd_old) == 1);
    stdlib_release(fd_p); /* fd_old is now stale; never dereference it */

    /* The tombstoned slot is C+'s free bit: no valid cap, empty metadata. */
    fd_check("fd_freed_hasvalidcap", stdlib_alloc_hasvalidcap(fd_old) == 0);
    fd_check("fd_freed_cap_state",
             stdlib_alloc_cap_state(fd_old) == STDLIB_ALLOC_CAP_NONE);
    fd_check("fd_freed_meta_zero",
             stdlib_alloc_generation(fd_old) == 0 &&
             stdlib_alloc_size(fd_old) == 0 &&
             stdlib_alloc_elem_size(fd_old) == 0);
    fd_check("fd_freed_covers",
             stdlib_alloc_cap_covers(fd_old, 0, 1) == 0 &&
             stdlib_alloc_cap_covers(fd_old, 0, 0) == 0);
    fd_check("fd_freed_cap_check",
             stdlib_alloc_cap_check(fd_old, fd_g, 0, 1) == 0);
    fd_check("fd_freed_identity_stale",
             stdlib_alloc_id_matches(&fd_id, fd_old) == 0);
}

/* ---- recycled address: address alone is not identity -------------------- */
static void fd_recycled_state(void) {
    void *fd_p = stdlib_alloc(40);
    void *fd_old = fd_p;
    unsigned long long fd_g;
    stdlib_alloc_id fd_id;
    void *fd_q;

    fd_g = stdlib_alloc_generation(fd_p);
    (void)stdlib_alloc_identity(fd_p, &fd_id);
    stdlib_release(fd_p);
    fd_q = stdlib_alloc(40);
    fd_check("fd_recycled_alloc", fd_q != NULL);

    /* Exactly one check per branch keeps output deterministic regardless of
     * whether the host allocator reuses the address. */
    if (fd_q == fd_old) {
        unsigned long long fd_g2 = stdlib_alloc_generation(fd_q);
        fd_check("fd_recycled_same_addr",
                 stdlib_alloc_hasvalidcap(fd_q) == 1 &&
                 fd_g2 > fd_g && fd_g2 != fd_g &&
                 stdlib_alloc_cap_check(fd_q, fd_g, 0, 1) == 0 &&
                 stdlib_alloc_cap_check(fd_q, fd_g2, 0, 1) == 1 &&
                 stdlib_alloc_id_matches(&fd_id, fd_q) == 0);
    } else {
        fd_check("fd_recycled_diff_addr",
                 stdlib_alloc_hasvalidcap(fd_old) == 0 &&
                 stdlib_alloc_hasvalidcap(fd_q) == 1 &&
                 stdlib_alloc_id_matches(&fd_id, fd_q) == 0);
    }
    stdlib_release(fd_q);
}

/* ---- overflow-safe bounds edges on a live base -------------------------- */
static void fd_bounds_edges(void) {
    void *fd_p = stdlib_alloc(16);
    unsigned long long fd_g = stdlib_alloc_generation(fd_p);

    fd_check("fd_bounds_exact_end",
             stdlib_alloc_cap_covers(fd_p, 16, 0) == 1);
    fd_check("fd_bounds_past_end",
             stdlib_alloc_cap_covers(fd_p, 17, 0) == 0 &&
             stdlib_alloc_cap_covers(fd_p, 15, 2) == 0 &&
             stdlib_alloc_cap_covers(fd_p, 0, 17) == 0);
    fd_check("fd_bounds_huge_size_no_wrap",
             stdlib_alloc_cap_covers(fd_p, 1, (size_t)-1) == 0);
    fd_check("fd_bounds_checked_cap_check",
             stdlib_alloc_cap_check(fd_p, fd_g, 0, 16) == 1 &&
             stdlib_alloc_cap_check(fd_p, fd_g, 0, 17) == 0);
    stdlib_release(fd_p);
}

/* ---- invalid-free refusal: untracked / NULL, no host deallocation ------- */
static void fd_invalid_refusal(void) {
    void *fd_u = (void *)fd_untracked_storage;
    stdlib_telemetry_stats fd_tm0;
    stdlib_telemetry_stats fd_tm1;
    void *fd_d = NULL;
    void *fd_s = fd_u;
    void *fd_live = stdlib_alloc(8);
    unsigned long long fd_lg = stdlib_alloc_generation(fd_live);

    fd_check("fd_refusal_untracked",
             stdlib_alloc_hasvalidcap(fd_u) == 0 &&
             stdlib_alloc_cap_state(fd_u) == STDLIB_ALLOC_CAP_NONE &&
             stdlib_alloc_cap_covers(fd_u, 0, 1) == 0 &&
             stdlib_alloc_generation(fd_u) == 0);

    /* A NULL capability is never valid, whatever the generation argument. */
    fd_check("fd_refusal_null",
             stdlib_alloc_hasvalidcap(NULL) == 0 &&
             stdlib_alloc_cap_state(NULL) == STDLIB_ALLOC_CAP_NONE &&
             stdlib_alloc_cap_covers(NULL, 0, 0) == 0 &&
             stdlib_alloc_cap_check(NULL, fd_lg, 0, 1) == 0);

    /* Interior pointers are not found: the base must be passed with an offset.
     * fd_untracked_storage + a checked offset is represented by (base, offset)
     * in this API, so the untracked base above covers the same refusal path. */

    /* stdlib_realloc validates via the side table FIRST: it refuses an
     * untracked pointer, reports a diagnostic, and performs no host free. */
    stdlib_telemetry_snapshot(&fd_tm0);
    fd_check("fd_refusal_realloc", stdlib_realloc(fd_u, 8) == NULL);
    stdlib_telemetry_snapshot(&fd_tm1);
    fd_check("fd_refusal_realloc_diagnosed",
             fd_tm1.invalid_ops == fd_tm0.invalid_ops + 1 &&
             fd_tm1.api_failures == fd_tm0.api_failures + 1 &&
             fd_tm1.free_count == fd_tm0.free_count &&
             stdlib_alloc_hasvalidcap(fd_u) == 0);

    /* The raw ownership helper refuses an untracked source without moving it. */
    fd_check("fd_refusal_transfer",
             stdlib_transfer_ownership(&fd_d, &fd_s) == -1 &&
             fd_d == NULL && fd_s == fd_u);

    stdlib_release(fd_live);
}

int main(void) {
    stdlib_telemetry_set_enabled(1);
    stdlib_alloc_fail_reset();
    stdlib_telemetry_reset();
    stdlib_set_error_callback(NULL);

    fd_live_state();
    fd_freed_state();
    fd_recycled_state();
    fd_bounds_edges();
    fd_invalid_refusal();

    fd_check("fd_terminated", 1);
    print("ALLOC_DIAG", fd_checks, fd_failed);
    return fd_failed ? 1 : 0;
}
