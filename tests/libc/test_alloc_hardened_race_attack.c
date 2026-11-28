/* Deterministic regressions for issue #196.
 *
 * 1. cplus_alloc_atomic_realloc(&p, 0) must not publish a dangling pointer:
 *    it refuses before touching the slot or the allocation.
 * 2. The hardened wrappers must check-and-act under one allocator lock. The
 *    internal generation-checked helpers are exercised directly with a stale
 *    generation: a stale call must refuse without touching the slot or the
 *    live allocation, while the current generation succeeds. A caller whose
 *    observed generation is invalidated by a concurrent free/realloc therefore
 *    cannot operate on a recycled address.
 *
 * Output: "alloc_hardened_race_attack: <checks> <failures>".
 */
#define _POSIX_C_SOURCE 200112L
#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "cplus_alloc_hardened.h"
#include "cplus_libc.h"
#include "../../libc/src/stdlib/alloc_internal.h"

static int g_checks;
static int g_failures;

#define CHECK(cond, name)                                                      \
    do {                                                                       \
        ++g_checks;                                                            \
        if (!(cond)) { ++g_failures; fprintf(stderr, "FAIL: %s\n", (name)); }  \
    } while (0)

#define CHECK_EQ(actual, expected, name)                                       \
    do {                                                                       \
        int a_ = (int)(actual);                                                \
        int e_ = (int)(expected);                                              \
        ++g_checks;                                                            \
        if (a_ != e_) {                                                        \
            ++g_failures;                                                      \
            fprintf(stderr, "FAIL: %s (got %d, expected %d)\n", (name), a_, e_); \
        }                                                                      \
    } while (0)

int main(void)
{
    void *p = NULL;
    void *saved;
    unsigned char *buf;
    cplus_alloc_meta m;
    unsigned long long gen;

    cplus_alloc_fail_reset();

    /* --- zero-size: never leave *ptr dangling ---------------------------- */
    CHECK_EQ(cplus_alloc_atomic_realloc(&p, 32), 0, "initial atomic allocation");
    CHECK(p != NULL, "initial allocation non-NULL");
    if (p == NULL) { printf("alloc_hardened_race_attack: %d %d\n", g_checks, g_failures); return 1; }
    saved = p;
    buf = (unsigned char *)p;
    memset(buf, 0xC3u, 32);

    errno = 0;
    CHECK_EQ(cplus_alloc_atomic_realloc(&p, 0), EINVAL, "zero-size refused");
    CHECK(errno == EINVAL, "zero-size sets EINVAL");
    CHECK(p == saved, "zero-size leaves slot unchanged (no dangling pointer)");
    CHECK(cplus_alloc_is_live(saved) == 1, "zero-size leaves block live");
    CHECK(buf[0] == 0xC3u && buf[31] == 0xC3u, "zero-size leaves bytes intact");

    /* The block is still usable through the same pointer afterwards. */
    CHECK_EQ(cplus_alloc_atomic_realloc(&p, 64), 0, "realloc after zero-size refusal");
    CHECK(p != NULL && cplus_alloc_size(p) == 64, "block grew after refusal");
    CHECK_EQ(cplus_alloc_checked_free(&p), 0, "checked free after refusal");
    CHECK(p == NULL, "successful checked free NULLs slot");

    /* --- generation-checked check-and-act ------------------------------- */
    p = NULL;
    CHECK_EQ(cplus_alloc_atomic_realloc(&p, 64), 0, "generation test allocation");
    CHECK(p != NULL, "generation test pointer");
    if (p == NULL) { printf("alloc_hardened_race_attack: %d %d\n", g_checks, g_failures); return 1; }
    CHECK_EQ(cplus_alloc_get_meta(p, &m), 0, "generation observed");
    gen = m.generation;
    saved = p;
    buf = (unsigned char *)p;
    memset(buf, 0x5Au, 64);

    CHECK_EQ(cplus_alloc_realloc_generation(&p, 128, gen + 1u),
             CPLUS_ALLOC_GEN_STALE, "stale generation refused");
    CHECK(p == saved, "stale realloc leaves slot unchanged");
    CHECK(cplus_alloc_is_live(saved) == 1, "stale realloc leaves block live");
    CHECK(cplus_alloc_size(saved) == 64, "stale realloc leaves size unchanged");
    CHECK(buf[0] == 0x5Au && buf[63] == 0x5Au, "stale realloc leaves bytes intact");

    CHECK_EQ(cplus_alloc_free_generation(&p, gen + 1u),
             CPLUS_ALLOC_GEN_STALE, "stale free refused by generation helper");
    CHECK(p == saved && cplus_alloc_is_live(saved) == 1,
          "stale free leaves live block and slot unchanged");

    /* The exact observed generation is still the one that owns the block. */
    CHECK_EQ(cplus_alloc_realloc_generation(&p, 128, gen), CPLUS_ALLOC_GEN_OK,
             "current generation accepted");
    CHECK(p != NULL && cplus_alloc_size(p) == 128, "current generation grew block");
    CHECK_EQ(cplus_alloc_get_meta(p, &m), 0, "new generation observed");
    CHECK(m.generation != gen, "realloc advanced generation");
    gen = m.generation;

    CHECK_EQ(cplus_alloc_free_generation(&p, gen), CPLUS_ALLOC_GEN_OK,
             "current generation freed");
    CHECK(p == NULL, "successful generation free NULLs slot");

    /* NULL slots and untracked pointers are refused without touching them. */
    CHECK_EQ(cplus_alloc_free_generation(&p, 0), CPLUS_ALLOC_GEN_INVALID,
             "NULL slot refused by generation helper");
    {
        int stack_obj = 0;
        void *bad = &stack_obj;
        CHECK_EQ(cplus_alloc_realloc_generation(&bad, 16, 0),
                 CPLUS_ALLOC_GEN_INVALID, "untracked pointer refused atomically");
        CHECK(bad == &stack_obj, "untracked slot unchanged");
    }

    cplus_alloc_fail_reset();
    printf("alloc_hardened_race_attack: %d %d\n", g_checks, g_failures);
    return g_failures != 0;
}
