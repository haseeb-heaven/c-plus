/* Runtime regression coverage for C+ compiler capability gaps.
 *
 * Source of the gap classes: tests/stdlib/compile_time/{string,vector,
 * collections,file,net,thread,error}.json plus the JSON borrowed-view notes in
 * docs/stdlib/DEEP_JSON_CONTRACT.md. Those suites prove that `c+ check
 * --strict` / `c+ build --strict` ACCEPT many unsafe shapes (drop()/close()
 * state, owner liveness, view/owner relationships, guard ownership, aliases,
 * released-pointer arguments). This file does NOT re-run the compiler. It
 * proves, with ordinary hardened C APIs and NO raw UB, what the runtime layer
 * actually does for the same shapes, so the mitigations are pinned by tests.
 *
 * Every scenario is a DEFINED runtime scenario:
 *   - allocation/free go through the tracked allocator (cplus_malloc,
 *     cplus_free_checked) and stale handles are NEVER dereferenced raw;
 *   - "use after drop" is attempted through a checked accessor that probes
 *     cplus_alloc_is_live()/cplus_alloc_check_generation() first;
 *   - capacity-checked writes on untracked pointers run with strict
 *     unknown-capacity mode, so they are REFUSED (errno=EINVAL) rather than
 *     delegated to the host memset;
 *   - double close uses the FILE* tracking table in cplus_fclose(), which
 *     rejects the second close before touching the host stream;
 *   - mutex "guards" are modelled with an owns/valid flag that is cleared
 *     before the OS primitive is touched, so no locked/destroyed mutex is
 *     ever unlocked by pthreads.
 *
 * Headers: stdlib/safe/safe_c.h (checked ordinary-C surface, format checks),
 * cplus_libc.h (explicit cplus_* checked entry points in the plain libc
 * layer), cplus_threads.h (C11 mutex surface), cplus_uchar.h (UTF typedef
 * surface). All called cplus_* symbols live in libcplus_hardened.a, so the
 * same source links with both libs, with plain libc only, and under UBSan.
 *
 * Build (from the repository root):
 *   CC=${CPLUS_CLANG:-clang}
 *   INC="-I. -Istdlib/safe -Ilibc -Ilibc/install-include"
 *   $CC -O1 -g -std=c99 -Wall -Wextra -pedantic $INC \
 *       tests/libc/test_compile_gaps_runtime.c \
 *       build/libc/libcplus_hardened.a build/safe/libcplus_safecrt.a \
 *       -lm -pthread -o build/libc/test_compile_gaps_runtime
 *   # drop the safecrt archive from that line for the "plain libc only" run;
 *   # add -fsanitize=undefined -fno-sanitize-recover=all for the UBSan run.
 *   # The safe_c.h include is relative to this file, so the narrower harness
 *   # include set (-Ilibc -Ilibc/include -Ilibc/install-include) also works.
 *
 * Output: one line, `compile_gaps_runtime: <N>/<N> checks ok`. Any failed
 * expectation also emits a FINDING line on stdout.
 */
#define _POSIX_C_SOURCE 200809L

/* Relative path keeps this test buildable with either the repo-root include
 * set (-I. -Istdlib/safe ...) or the narrower tests/libc harness set
 * (-Ilibc -Ilibc/include -Ilibc/install-include). safe_c.h's own quoted
 * includes resolve inside stdlib/safe/; cplus_*.h resolve via -Ilibc/install-include. */
#include "../../stdlib/safe/safe_c.h"
#include "cplus_libc.h"
#include "cplus_threads.h"
#include "cplus_uchar.h"

#include <errno.h>
#include <fcntl.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* ------------------------------------------------------------- reporting -- */
static int g_checks;
static int g_fails;

static void expect(int cond, const char *what) {
    ++g_checks;
    if (!cond) {
        ++g_fails;
        printf("FINDING: %s\n", what);
    }
}
#define EXPECT(c) expect((c), #c)

/* A digest captures only deterministic observables (returns, errno, boolean
 * state, counter DELTAS). It is filled once with logging OFF and once with
 * logging ON; equality is the logging ON/OFF parity proof. */
#define DIG_CAP 96
typedef struct {
    int m[DIG_CAP];
    int n;
} digest_t;

static void dig(digest_t *d, int value) {
    if (d->n < DIG_CAP) d->m[d->n++] = value;
}

static int digest_equal(const digest_t *a, const digest_t *b) {
    int i;
    if (a->n != b->n) {
        printf("FINDING: logging parity digest length %d != %d\n", a->n, b->n);
        return 0;
    }
    for (i = 0; i < a->n; ++i) {
        if (a->m[i] != b->m[i]) {
            printf("FINDING: logging parity digest[%d]: %d != %d\n",
                   i, a->m[i], b->m[i]);
            return 0;
        }
    }
    return 1;
}

/* ------------------------------------------------------- checked accessors - */
/* Reads one byte only when BOTH liveness and the captured generation still
 * match. This is the C-level counterpart of a checked string/vector/json
 * view accessor: a stale view returns -1/EINVAL without touching memory. */
static int checked_byte(const unsigned char *p, size_t off, size_t len,
                        const void *base, unsigned long long gen,
                        unsigned char *out) {
    if (p == NULL || base == NULL || out == NULL) {
        errno = EINVAL;
        return -1;
    }
    if (!cplus_alloc_is_live(base)) {
        errno = EINVAL;
        return -1;
    }
    if (!cplus_alloc_check_generation(base, gen)) {
        errno = EINVAL;
        return -1;
    }
    if (off >= len || cplus_alloc_remaining_capacity(p) <= off) {
        errno = ERANGE;
        return -1;
    }
    *out = p[off];
    return 0;
}

/* ------------------------------------------- GAP: use-after-drop / -close -- */
/* string_use_after_drop, vector_use_after_drop, collections_use_after_drop,
 * file_read_after_close, net_use_after_close, error_use_after_reset.
 * Runtime mitigation: drop()/close() leaves an inert handle; the tracked
 * allocator reports the dead state and checked accessors refuse. */
static void sc_use_after_drop(digest_t *d) {
    unsigned char *h = (unsigned char *)cplus_malloc(32);
    unsigned char *stale;
    cplus_alloc_meta meta;
    unsigned long long gen0;
    unsigned char out = 0xA5;
    int rc;

    EXPECT(h != NULL);
    if (h == NULL) return;

    errno = 0;
    EXPECT(cplus_memset_bounded(h, 0xA5, 32, 32) == h && errno == 0);
    EXPECT(cplus_alloc_get_meta(h, &meta) == 0 && meta.is_live == 1 &&
           meta.byte_size == 32 && meta.generation != 0);
    gen0 = meta.generation;
    dig(d, (meta.generation != 0));

    /* Positive control: live owner + view reads succeed. */
    errno = 0;
    rc = checked_byte(h, 0, 32, h, gen0, &out);
    EXPECT(rc == 0 && out == 0xA5 && errno == 0);
    dig(d, rc);
    dig(d, out);

    /* drop(): handle is zeroed, stale alias is dead. */
    stale = h;
    errno = 0;
    rc = cplus_free_checked((void **)&h);
    EXPECT(rc == 0 && h == NULL && errno == 0);
    dig(d, rc);
    dig(d, h == NULL);

    EXPECT(cplus_alloc_is_live(stale) == 0);
    dig(d, cplus_alloc_is_live(stale));
    errno = 0;
    rc = cplus_alloc_get_meta(stale, &meta);
    EXPECT(rc == EINVAL && errno == EINVAL);
    EXPECT(meta.is_live == 0 && meta.byte_size == 0 && meta.generation == 0);
    dig(d, rc);
    dig(d, errno);
    dig(d, meta.is_live);

    /* Checked "use after drop": refused before any dereference. */
    out = 0xA5;
    errno = 0;
    rc = checked_byte(stale, 0, 32, stale, gen0, &out);
    EXPECT(rc == -1 && errno == EINVAL && out == 0xA5);
    dig(d, rc);
    dig(d, errno);

    /* Capacity-checked write on an untracked (freed) pointer, strict mode:
     * refused with EINVAL, not forwarded to raw memset. */
    errno = 0;
    rc = (cplus_memset_bounded(stale, 0x5A, 32, (size_t)-1) == NULL);
    EXPECT(rc == 1 && errno == EINVAL);
    dig(d, rc);
    dig(d, errno);
}

/* ------------------------------------------------ GAP: double-drop / -free -- */
/* string_double_drop, net_double_close (allocation analogue), plus invalid
 * and duplicate free refusal through the checked API. */
static void sc_double_drop(digest_t *d) {
    void *h = cplus_malloc(24);
    void *stale;
    unsigned long df0 = cplus_alloc_double_free_count();
    unsigned long inv0 = cplus_alloc_invalid_free_count();
    int stack_obj = 7;
    void *bad = &stack_obj;
    int rc;

    EXPECT(h != NULL);
    if (h == NULL) return;
    stale = h;

    errno = 0;
    EXPECT(cplus_free_checked(&h) == 0 && h == NULL && errno == 0);
    dig(d, h == NULL);
    EXPECT(cplus_alloc_is_live(stale) == 0);
    dig(d, cplus_alloc_is_live(stale));

    /* Second drop of the same address is refused, not performed twice. */
    errno = 0;
    rc = cplus_free_checked(&stale);
    EXPECT(rc == EINVAL && stale == NULL && errno == EINVAL);
    EXPECT(cplus_alloc_double_free_count() == df0 + 1);
    dig(d, rc);
    dig(d, errno);
    dig(d, (int)(cplus_alloc_double_free_count() - df0));

    /* Handle nulled by the refusal is inert from then on. */
    errno = 0;
    EXPECT(cplus_free_checked(&stale) == 0 && errno == 0);
    EXPECT(cplus_alloc_double_free_count() == df0 + 1);
    dig(d, errno);
    dig(d, (int)(cplus_alloc_double_free_count() - df0));

    /* Unowned stack address is refused as an invalid free. */
    errno = 0;
    rc = cplus_free_checked(&bad);
    EXPECT(rc == EINVAL && bad == NULL && errno == EINVAL);
    EXPECT(cplus_alloc_invalid_free_count() == inv0 + 1);
    dig(d, rc);
    dig(d, (int)(cplus_alloc_invalid_free_count() - inv0));
}

/* ------------------------------ GAP: dangling views / slices / key buffer -- */
/* string_dangling_view, vector_invalid_slice, collections_dangling_key and
 * vector_const_oob (checked accessor rejects the bad index). Runtime
 * mitigation: a view carries base+generation; stale detection is generation
 * based, so address recycling cannot revive it. */
static void sc_dangling_view(digest_t *d) {
    unsigned char *h = (unsigned char *)cplus_malloc(32);
    unsigned char *stale;
    unsigned char *key, *stored_key;
    unsigned char *recycled;
    cplus_alloc_meta meta;
    unsigned long long gen;
    unsigned char out = 0;
    int rc;

    EXPECT(h != NULL);
    if (h == NULL) return;
    errno = 0;
    EXPECT(cplus_memset_bounded(h, 0x3C, 32, 32) == h && errno == 0);
    EXPECT(cplus_alloc_get_meta(h, &meta) == 0);
    gen = meta.generation;

    /* Live view at base and at an interior offset (JSON child / vector slice). */
    errno = 0;
    rc = checked_byte(h, 2, 32, h, gen, &out);
    EXPECT(rc == 0 && out == 0x3C && errno == 0);
    dig(d, rc);
    dig(d, out);
    EXPECT(cplus_alloc_remaining_capacity(h) == 32);
    EXPECT(cplus_alloc_remaining_capacity(h + 8) == 24);
    dig(d, (int)(cplus_alloc_remaining_capacity(h + 8) == 24));
    errno = 0;
    rc = checked_byte(h + 8, 0, 24, h, gen, &out);
    EXPECT(rc == 0 && errno == 0);
    dig(d, rc);

    /* Out-of-range checked index is refused (vector_const_oob analogue). */
    errno = 0;
    rc = checked_byte(h, 32, 32, h, gen, &out);
    EXPECT(rc == -1 && errno == ERANGE);
    dig(d, rc);
    dig(d, errno);

    /* Drop owner: both views go stale, interior capacity disappears. */
    stale = h;
    errno = 0;
    EXPECT(cplus_free_checked((void **)&h) == 0 && errno == 0);
    EXPECT(cplus_alloc_is_live(stale) == 0);
    EXPECT(cplus_alloc_check_generation(stale, gen) == 0);
    dig(d, cplus_alloc_is_live(stale));
    dig(d, cplus_alloc_check_generation(stale, gen));
    EXPECT(cplus_alloc_remaining_capacity(stale + 8) == (size_t)-1);
    dig(d, (int)(cplus_alloc_remaining_capacity(stale + 8) == (size_t)-1));

    errno = 0;
    rc = checked_byte(stale, 2, 32, stale, gen, &out);
    EXPECT(rc == -1 && errno == EINVAL && out == 0x3C);
    dig(d, rc);
    dig(d, errno);

    /* Address recycling cannot revive a stale view: whether the allocator
     * reuses the address or not, the captured generation no longer matches. */
    recycled = (unsigned char *)cplus_malloc(32);
    EXPECT(recycled != NULL);
    dig(d, cplus_alloc_check_generation(stale, gen) == 0);
    dig(d, (recycled != NULL));
    if (recycled != NULL) {
        EXPECT(cplus_alloc_is_live(recycled) == 1);
        errno = 0;
        EXPECT(cplus_free_checked((void **)&recycled) == 0 && errno == 0);
    }

    /* Dangling key (collections_dangling_key): the map copies keys, so it
     * survives; a lookup with the caller's RELEASED key is refused by the
     * checked probe instead of hashing freed memory. */
    key = (unsigned char *)cplus_malloc(8);
    stored_key = (unsigned char *)cplus_malloc(8);
    EXPECT(key != NULL && stored_key != NULL);
    if (key != NULL && stored_key != NULL) {
        EXPECT(cplus_memcpy_bounded(key, "KEY12345", 8, 8) == key);
        EXPECT(cplus_memcpy_bounded(stored_key, key, 8, 8) == stored_key);
        EXPECT(cplus_alloc_get_meta(key, &meta) == 0);
        gen = meta.generation;
        errno = 0;
        rc = checked_byte(key, 0, 8, key, gen, &out);
        EXPECT(rc == 0 && out == 'K' && errno == 0);
        dig(d, rc);

        stale = key;
        errno = 0;
        EXPECT(cplus_free_checked((void **)&key) == 0 && errno == 0);
        errno = 0;
        rc = checked_byte(stale, 0, 8, stale, gen, &out);
        EXPECT(rc == -1 && errno == EINVAL);
        dig(d, rc);
        dig(d, errno);

        EXPECT(cplus_alloc_get_meta(stored_key, &meta) == 0);
        errno = 0;
        rc = checked_byte(stored_key, 0, 8, stored_key, meta.generation, &out);
        EXPECT(rc == 0 && out == 'K' && errno == 0);
        dig(d, rc);
        dig(d, out);
        errno = 0;
        EXPECT(cplus_free_checked((void **)&stored_key) == 0 && errno == 0);
    }
}

/* ------------------------------------------------- GAP: double-close (file) -- */
/* file_double_close, file_write_after_close, file_read_after_close.
 * Runtime mitigation: cplus_fclose() records closed FILE* addresses and
 * refuses a repeat before calling host fclose (EOF/EBADF). A checked file
 * wrapper refuses reads after close without touching the closed FILE*. */
typedef struct {
    FILE *fp;
    int open;
} checked_file_t;

static int cf_write(checked_file_t *f, const void *buf, size_t n) {
    size_t w;
    if (f == NULL || !f->open || f->fp == NULL) {
        errno = EBADF;
        return -1;
    }
    w = cplus_fwrite(buf, 1, n, f->fp);
    return (w == n) ? 0 : -1;
}

static int cf_read(checked_file_t *f, void *buf, size_t n) {
    size_t r;
    if (f == NULL || !f->open || f->fp == NULL) {
        errno = EBADF;
        return -1;
    }
    r = cplus_fread(buf, 1, n, f->fp);
    return (r == n) ? 0 : -1;
}

static void sc_double_close(digest_t *d, int pass) {
    char path[128];
    const char *tmp = getenv("TMPDIR");
    checked_file_t f;
    char buf[4] = {0};
    int rc;

    if (tmp == NULL || *tmp == '\0') tmp = "/tmp";
    (void)cplus_snprintf(path, sizeof(path), "%s/cplus_compile_gaps_%ld_%d.tmp",
                         tmp, (long)getpid(), pass);

    f.fp = cplus_fopen(path, "w+b");
    f.open = (f.fp != NULL);
    EXPECT(f.fp != NULL);
    if (f.fp == NULL) return;

    errno = 0;
    EXPECT(cf_write(&f, "XY", 2) == 0 && errno == 0);
    dig(d, errno);
    errno = 0;
    EXPECT(cf_read(&f, buf, 2) == -1); /* at EOF after write; defined failure */
    dig(d, buf[0] == 0);

    /* First close is the only one that reaches host fclose(). */
    errno = 0;
    rc = cplus_fclose(f.fp);
    EXPECT(rc == 0 && errno == 0);
    f.open = 0;
    dig(d, rc);
    dig(d, errno);

    /* Second and third close are refused with EOF/EBADF. */
    errno = 0;
    rc = cplus_fclose(f.fp);
    EXPECT(rc == EOF && errno == EBADF);
    dig(d, rc);
    dig(d, errno);
    errno = 0;
    rc = cplus_fclose(f.fp);
    EXPECT(rc == EOF && errno == EBADF);
    dig(d, rc);
    dig(d, errno);

    /* NULL stream is refused with EOF/EINVAL, no crash. */
    errno = 0;
    rc = cplus_fclose(NULL);
    EXPECT(rc == EOF && errno == EINVAL);
    dig(d, rc);
    dig(d, errno);

    /* Checks wrapper refuses post-close without touching the FILE*. */
    errno = 0;
    EXPECT(cf_read(&f, buf, 2) == -1 && errno == EBADF);
    EXPECT(cf_write(&f, "Z", 1) == -1 && errno == EBADF);
    dig(d, errno);
    dig(d, errno);

    (void)cplus_remove(path);
}

/* --------------------------------------------------- GAP: mutex guards -- */
/* thread_double_unlock, thread_unlock_without_ownership,
 * thread_guard_after_destroy, thread_invalid_mutex_owner.
 * Runtime mitigation: a guard owns a flag that is cleared on the first
 * unlock / when the mutex is dropped; the OS primitive is never touched
 * with a stale or unowned handle. NULL/invalid-type calls are refused by
 * cplus_mtx_* with CPLUS_THRD_ERROR. */
typedef struct {
    cplus_mtx_t *m;
    int owns;
    int valid;
} guard_t;

static int guard_lock(guard_t *g) {
    int rc;
    if (g == NULL || g->m == NULL || !g->valid || g->owns) return -1;
    rc = cplus_mtx_lock(g->m);
    if (rc == CPLUS_THRD_SUCCESS) g->owns = 1;
    return rc;
}

static int guard_unlock(guard_t *g) {
    int rc;
    if (g == NULL || !g->owns || g->m == NULL || !g->valid) return -1;
    rc = cplus_mtx_unlock(g->m);
    if (rc == CPLUS_THRD_SUCCESS) g->owns = 0;
    return rc;
}

static void sc_mutex_guard(digest_t *d) {
    cplus_mtx_t m;
    cplus_mtx_t bad;
    guard_t g;

    errno = 0;
    EXPECT(cplus_mtx_init(&m, CPLUS_MTX_PLAIN) == CPLUS_THRD_SUCCESS);
    dig(d, errno);

    /* Positive control: real lock state is observable via trylock. */
    EXPECT(cplus_mtx_lock(&m) == CPLUS_THRD_SUCCESS);
    EXPECT(cplus_mtx_trylock(&m) == CPLUS_THRD_BUSY);
    dig(d, cplus_mtx_trylock(&m) == CPLUS_THRD_BUSY);
    EXPECT(cplus_mtx_unlock(&m) == CPLUS_THRD_SUCCESS);

    /* Valid guard: one unlock; the second is inert. */
    g.m = &m;
    g.owns = 0;
    g.valid = 1;
    EXPECT(guard_lock(&g) == CPLUS_THRD_SUCCESS);
    EXPECT(guard_unlock(&g) == CPLUS_THRD_SUCCESS);
    dig(d, g.owns);
    EXPECT(guard_unlock(&g) == -1 && g.owns == 0);
    dig(d, guard_unlock(&g));

    /* Unowned guard unlock is a guarded no-op (no OS call). */
    {
        guard_t empty;
        empty.m = NULL;
        empty.owns = 0;
        empty.valid = 0;
        EXPECT(guard_unlock(&empty) == -1);
        dig(d, guard_unlock(&empty));
        EXPECT(cplus_mtx_unlock(NULL) == CPLUS_THRD_ERROR);
        dig(d, cplus_mtx_unlock(NULL));
        EXPECT(cplus_mtx_lock(NULL) == CPLUS_THRD_ERROR);
        dig(d, cplus_mtx_lock(NULL));
    }

    /* Guard after mutex destroy: release, destroy, invalidate, then unlock. */
    EXPECT(guard_lock(&g) == CPLUS_THRD_SUCCESS);
    EXPECT(cplus_mtx_unlock(&m) == CPLUS_THRD_SUCCESS);
    cplus_mtx_destroy(&m);
    g.m = NULL; /* models mutex::drop() NULLing its handle */
    g.valid = 0;
    EXPECT(g.owns == 1); /* the guard still *thinks* it owns */
    EXPECT(guard_unlock(&g) == -1 && g.owns == 1);
    dig(d, guard_unlock(&g));
    dig(d, g.owns);

    /* Guard over a zero-initialised (invalid) mutex is inert. */
    (void)cplus_memset(&bad, 0, sizeof(bad));
    {
        guard_t invalid;
        invalid.m = &bad;
        invalid.owns = 0;
        invalid.valid = 0;
        EXPECT(guard_lock(&invalid) == -1);
        EXPECT(guard_unlock(&invalid) == -1);
        dig(d, guard_lock(&invalid));
        dig(d, guard_unlock(&invalid));
        EXPECT(cplus_mtx_init(&bad, 99) == CPLUS_THRD_ERROR);
        dig(d, cplus_mtx_init(&bad, 99));
    }
}

/* ------------------------------------------------ GAP: collection aliases -- */
/* collections_mutable_alias, collections_use_after_move, vector_use_after_move
 * and the untagged copy rejection. Runtime mitigation: aliasing one owner
 * makes both aliases dead after a single drop; transfer bumps the generation
 * so a pre-move alias is detected even though the address is still live. */
static void sc_collections_alias(digest_t *d) {
    int *a = (int *)cplus_calloc(8, sizeof(int));
    int *b;
    int *stale;
    int *dst = NULL;
    int *other;
    cplus_alloc_meta meta;
    unsigned long long gen0;
    int val = 7;
    int rc;

    EXPECT(a != NULL);
    if (a == NULL) return;
    b = a; /* both are mutable aliases of the same owner */

    EXPECT(cplus_alloc_get_meta(a, &meta) == 0 && meta.elem_count == 8 &&
           meta.elem_size == sizeof(int));
    gen0 = meta.generation;
    errno = 0;
    EXPECT(cplus_memcpy_bounded(&a[0], &val, sizeof(val),
                                cplus_alloc_remaining_capacity(a)) == &a[0]);
    EXPECT(a[0] == 7 && b[0] == 7);
    dig(d, a[0] == b[0]);

    /* Alias a drops the owner; alias b observes the dead state. */
    errno = 0;
    rc = cplus_free_checked((void **)&a);
    EXPECT(rc == 0 && a == NULL && errno == 0);
    EXPECT(cplus_alloc_is_live(b) == 0);
    EXPECT(cplus_alloc_check_generation(b, gen0) == 0);
    dig(d, cplus_alloc_is_live(b));
    dig(d, cplus_alloc_check_generation(b, gen0));
    errno = 0;
    EXPECT(cplus_alloc_get_meta(b, &meta) == EINVAL && errno == EINVAL);
    dig(d, errno);

    /* Move: generation changes while the address stays live. */
    a = (int *)cplus_calloc(4, sizeof(int));
    EXPECT(a != NULL);
    if (a == NULL) return;
    EXPECT(cplus_alloc_get_meta(a, &meta) == 0);
    gen0 = meta.generation;
    stale = a;
    errno = 0;
    rc = cplus_transfer_ownership((void **)&dst, (void **)&a);
    EXPECT(rc == 0 && a == NULL && dst != NULL && errno == 0);
    dig(d, rc);
    dig(d, a == NULL);
    EXPECT(cplus_alloc_is_live(stale) == 1); /* address still live */
    EXPECT(cplus_alloc_check_generation(dst, gen0) == 0);
    dig(d, cplus_alloc_is_live(stale));
    dig(d, cplus_alloc_check_generation(dst, gen0));
    EXPECT(cplus_alloc_get_meta(dst, &meta) == 0 && meta.generation != gen0);
    dig(d, meta.generation != gen0);

    /* Refuse to overwrite an existing owner (copy of owner is rejected). */
    other = (int *)cplus_calloc(2, sizeof(int));
    EXPECT(other != NULL);
    if (other != NULL) {
        errno = 0;
        EXPECT(cplus_transfer_ownership((void **)&dst, (void **)&other) == EINVAL);
        EXPECT(errno == EINVAL && other != NULL);
        dig(d, errno);
        errno = 0;
        EXPECT(cplus_free_checked((void **)&other) == 0 && errno == 0);
    }
    errno = 0;
    EXPECT(cplus_free_checked((void **)&dst) == 0 && errno == 0);
}

/* ------------------------------------------------ GAP: JSON borrowed view -- */
/* docs/stdlib/DEEP_JSON_CONTRACT.md section 2.4: a json_view borrows from its
 * json doc, has no owner reference and is accepted after the doc is dropped.
 * Runtime mitigation: the view carries the doc's base+generation; a checked
 * view read refuses after drop, and a re-created doc at a recycled address
 * cannot revive the old view. */
static void sc_json_borrow(digest_t *d) {
    unsigned char *doc = (unsigned char *)cplus_malloc(64);
    unsigned char *stale;
    unsigned char *reborn;
    cplus_alloc_meta meta;
    unsigned long long doc_gen;
    unsigned char out = 0;
    int rc;

    EXPECT(doc != NULL);
    if (doc == NULL) return;
    EXPECT(cplus_memcpy_bounded(doc, "{\"a\":1}", 7, 7) == doc);
    EXPECT(cplus_alloc_get_meta(doc, &meta) == 0);
    doc_gen = meta.generation;

    /* Positive control: borrow consumed while the doc is alive, incl. an
     * interior (child value) borrow with tracked remaining capacity. */
    errno = 0;
    rc = checked_byte(doc, 0, 64, doc, doc_gen, &out);
    EXPECT(rc == 0 && out == '{' && errno == 0);
    dig(d, rc);
    dig(d, out);
    errno = 0;
    rc = checked_byte(doc + 5, 0, 59, doc, doc_gen, &out);
    EXPECT(rc == 0 && out == '1' && errno == 0);
    dig(d, rc);
    dig(d, out);
    dig(d, (int)(cplus_alloc_remaining_capacity(doc + 5) == 59));

    /* Drop doc: the view is stale, checked reads refuse. */
    stale = doc;
    errno = 0;
    EXPECT(cplus_free_checked((void **)&doc) == 0 && errno == 0);
    EXPECT(cplus_alloc_check_generation(stale, doc_gen) == 0);
    dig(d, cplus_alloc_check_generation(stale, doc_gen));
    errno = 0;
    rc = checked_byte(stale + 5, 0, 59, stale, doc_gen, &out);
    EXPECT(rc == -1 && errno == EINVAL && out == '1');
    dig(d, rc);
    dig(d, errno);

    /* Re-create a doc; an old view must not become valid again. */
    reborn = (unsigned char *)cplus_malloc(64);
    EXPECT(reborn != NULL);
    if (reborn != NULL) {
        EXPECT(cplus_alloc_get_meta(reborn, &meta) == 0);
        EXPECT(cplus_alloc_check_generation(reborn, doc_gen) == 0);
        dig(d, cplus_alloc_check_generation(reborn, doc_gen));
        dig(d, meta.generation != doc_gen);
        errno = 0;
        EXPECT(cplus_free_checked((void **)&reborn) == 0 && errno == 0);
    }
}

/* ------------------------------------------- header surface (uchar.h) ----- */
/* cplus_uchar.h declares the mbrtoc16/c16rtomb/mbrtoc32/c32rtomb family but
 * NO backend is shipped (calling one is a link error). Only the typedefs are
 * exercised here; this is recorded as a warning in the companion doc. */
static void sc_header_surface(digest_t *d) {
    char16_t u16 = (char16_t)0x0041;
    char32_t u32 = (char32_t)0x0001F600u;
    EXPECT(sizeof(char16_t) >= 2 && sizeof(char32_t) >= 4);
    EXPECT(u16 == (char16_t)0x0041 && u32 == (char32_t)0x0001F600u);
    dig(d, (int)sizeof(char16_t));
    dig(d, (int)sizeof(char32_t));
}

/* ------------------------------------------------------------------ driver -- */
static void run_all_off(digest_t *d) {
    sc_use_after_drop(d);
    sc_double_drop(d);
    sc_dangling_view(d);
    sc_double_close(d, 0);
    sc_mutex_guard(d);
    sc_collections_alias(d);
    sc_json_borrow(d);
    sc_header_surface(d);
}

static void run_all_on(digest_t *d) {
    sc_use_after_drop(d);
    sc_double_drop(d);
    sc_dangling_view(d);
    sc_double_close(d, 1);
    sc_mutex_guard(d);
    sc_collections_alias(d);
    sc_json_borrow(d);
    sc_header_surface(d);
}

int main(void) {
    digest_t off_d, on_d;
    int saved_stderr = -1;
    int nullfd;

    cplus_memset(&off_d, 0, sizeof(off_d));
    cplus_memset(&on_d, 0, sizeof(on_d));

    /* Strict unknown-capacity mode makes checked writes on freed/untracked
     * pointers a defined refusal (EINVAL) instead of a raw host write. */
    cplus_libc_set_unknown_capacity_mode(1);

    /* Pass 1: logging OFF (the default). */
    cplus_libc_log_set_enabled(0);
    run_all_off(&off_d);
    EXPECT(off_d.n > 0 && off_d.n < DIG_CAP);
    dig(&off_d, 0); /* keep both digests the same length */

    /* Pass 2: identical scenarios with logging ON, fd 2 silenced so the
     * single result line is the only stdout output. */
    saved_stderr = dup(STDERR_FILENO);
    nullfd = open("/dev/null", O_WRONLY);
    if (nullfd >= 0) {
        (void)dup2(nullfd, STDERR_FILENO);
        (void)close(nullfd);
    }
    cplus_libc_log_set_enabled(1);
    run_all_on(&on_d);
    cplus_libc_log_set_enabled(0);
    if (saved_stderr >= 0) {
        fflush(stderr);
        (void)dup2(saved_stderr, STDERR_FILENO);
        (void)close(saved_stderr);
    }
    dig(&on_d, 0);

    /* Logging ON/OFF parity: returns, errno, state and counter deltas match. */
    EXPECT(digest_equal(&off_d, &on_d));

    cplus_libc_set_unknown_capacity_mode(0);
    printf("compile_gaps_runtime: %d/%d checks ok\n",
           g_checks - g_fails, g_checks);
    return g_fails == 0 ? 0 : 1;
}
