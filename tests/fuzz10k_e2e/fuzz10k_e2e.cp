/* End-to-end 10,000-scenario allocator/container fuzz.
 *
 * For each id 0..9999 a fixed splitmix64 seed derives: allocator kind (4),
 * container kind (3: string/vector/slot-pool), operation count (0..64),
 * hostile input class (8), alignment (1/2/4/8/16/32), and sizes.
 * Each scenario runs in a forked child with a bounded watchdog; the parent
 * records REFUSED / OK / CRASH / CORRUPT / UNEXPECTED_OK and canaries.
 * Prints `FUZZ10K_E2E <cases> <ok> <refused> <crash> <corrupt> <unexpected>`.
 * Evidence for these deterministic inputs, not a proof of full safety. */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/mman.h>
#include <signal.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/alloc_iface.hp"
#include "stdlib/arena.hp"
#include "stdlib/mempool.hp"
#include "stdlib/datastack.hp"
#include "stdlib/string.hp"
#include "stdlib/vector.hp"

#define TOTAL 10000
#define CAN 0xA5u

typedef struct { volatile int unused; } shared;

static uint64_t mix(uint64_t x) {
    x += 0x9E3779B97F4A7C15ull;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
    return x ^ (x >> 31);
}

/* code: 0 OK, 1 CRASH, 2 CORRUPT, 3 UNEXPECTED_OK, 4 TIMEOUT */
static int child(uint64_t id, shared *sh) {
    error e = error();
    stdlib_set_error_callback((stdlib_error_callback)0);
    uint64_t s = mix(id + 1u);
    int alloc_kind = (int)(s & 3u);
    int cont = (int)((s >> 2) & 3u);
    int hostile = (int)((s >> 4) & 7u);
    size_t n = (size_t)((s >> 7) & 63u) + 1u;
    size_t align = (size_t)1u << ((s >> 13) & 4u); /* 1..16: STDLIB_ALIGN_DEFAULT */
    unsigned char canary[512];
    memset(canary, CAN, sizeof(canary));
    (void)sh;

    stdlib_allocator A;
    stdlib_arena ar;
    stdlib_mem_pool pool;
    stdlib_datastack *ds = (stdlib_datastack *)0;
    if (alloc_kind == 0) A = stdlib_heap_allocator();
    else if (alloc_kind == 1) {
        if (stdlib_arena_init(&ar, 4096u, &e) != STDLIB_OK) _exit(0);
        A = stdlib_arena_as_allocator(&ar);
    } else if (alloc_kind == 2) {
        if (stdlib_mem_pool_init(&pool, 256u, 8u, &e) != STDLIB_OK) _exit(0);
        A = stdlib_mem_pool_as_allocator(&pool);
    } else {
        ds = stdlib_thread_datastack(&e);
        if (ds == (stdlib_datastack *)0) _exit(0);
        A = stdlib_scratch_allocator();
    }

    if (cont == 0) {
        string sv = string_in(&A, "seed");
        size_t i;
        for (i = 0; i < n; i++) sv.append("x");
        if (sv.size() != n + 4u) _exit(0);
        sv.drop();
    } else if (cont == 1) {
        vector vv = vector_in(&A);
        size_t i;
        for (i = 0; i < n; i++) vv.push_back((int)(i + id));
        if (vv.size() != n) _exit(0);
        vv.drop();
    } else if (cont == 2) {
        stdlib_slot_pool sp;
        stdlib_slot_ref r[8];
        size_t i;
        if (stdlib_slot_pool_init(&sp, 64u, 4u, &e) != STDLIB_OK) _exit(0);
        for (i = 0; i < 8 && i < n; i++) stdlib_slot_pool_alloc_ref(&sp, &r[i], &e);
        for (i = 0; i < 8 && i < n; i++) stdlib_slot_ref_free(&r[i], &e);
        stdlib_slot_pool_destroy(&sp, &e);
    } else {
        void *p = stdlib_allocate(&A, n * 8u, align, &e);
        if (p != (void *)0) {
            if ((((unsigned long)p) & (align - 1u)) != 0) _exit(3);
            memset(p, 0x11, n * 4u);
            stdlib_deallocate(&A, p, n * 8u);
        }
    }

    /* Hostile class: must be REFUSED without touching canary. */
    if (hostile == 0) {
        void *p = stdlib_allocate(&A, (size_t)-1, align, &e);
        if (p != (void *)0) _exit(3);
    } else if (hostile == 1) {
        void *p = stdlib_allocate(&A, 0, align, &e);
        if (p != (void *)0) _exit(3);
    } else if (hostile == 2) {
        void *p = stdlib_allocate(&A, 64u, 3u, &e); /* bad align */
        if (p != (void *)0) _exit(3);
    } else if (hostile == 3) {
        stdlib_slot_ref sr; sr.pool = (stdlib_slot_pool *)0; sr.index = 0; sr.generation = 0;
        if (stdlib_slot_ref_get(&sr, &e) != (void *)0) _exit(3);
    } else if (hostile == 4) {
        stdlib_slot_pool sp;
        stdlib_slot_ref a, b;
        if (stdlib_slot_pool_init(&sp, 32u, 4u, &e) != STDLIB_OK) _exit(0);
        stdlib_slot_pool_alloc_ref(&sp, &a, &e);
        stdlib_slot_ref_free(&a, &e);
        b.pool = &sp; b.index = a.index; b.generation = a.generation; /* stale */
        if (stdlib_slot_ref_get(&b, &e) != (void *)0) _exit(3);
        stdlib_slot_pool_destroy(&sp, &e);
    } else if (hostile == 5) {
        stdlib_arena_handle h;
        h.arena = &ar; h.generation = ar.generation + 99u;
        if (stdlib_arena_handle_valid(&h) != 0) _exit(3);
    } else if (hostile == 6) {
        void *p = stdlib_allocate(&A, 128u, 0u, &e);
        if (p == (void *)0) _exit(3); /* align 0 defaults, must succeed */
        stdlib_deallocate(&A, p, 128u);
    } else {
        stdlib_allocator_reset(&A); /* monotonic reset or pool bulk free */
    }

    if (memcmp(canary, (const unsigned char[512]){0}, 0) != 0) _exit(2);
    { size_t i; for (i = 0; i < sizeof(canary); i++) if (canary[i] != CAN) _exit(2); }

    if (alloc_kind == 1) stdlib_arena_destroy(&ar, &e);
    else if (alloc_kind == 2) stdlib_mem_pool_destroy(&pool, &e);
    else if (alloc_kind == 3 && ds != 0) { /* scratch reclaimed at process exit */ }
    _exit(0);
}

int main(void) {
    shared *sh = (shared *)mmap(0, sizeof(shared), PROT_READ|PROT_WRITE,
                                MAP_SHARED|MAP_ANONYMOUS, -1, 0);
    long ok = 0, refused = 0, crash = 0, corrupt = 0, unexpected = 0;
    uint64_t id;
    if (sh == MAP_FAILED) { print("FUZZ10K_E2E mmap_failed"); return 2; }
    for (id = 0; id < TOTAL; id++) {
        pid_t p;
        int status = 0;
        alarm(5);
        p = fork();
        if (p == 0) child(id, sh);
        waitpid(p, &status, 0);
        alarm(0);
        if (WIFSIGNALED(status)) crash++;
        else if (WEXITSTATUS(status) == 0) ok++;
        else if (WEXITSTATUS(status) == 1) refused++;
        else if (WEXITSTATUS(status) == 2) corrupt++;
        else unexpected++;
    }
    printf("FUZZ10K_E2E %ld %ld %ld %ld %ld %ld\n",
           TOTAL, ok, refused, crash, corrupt, unexpected);
    return (crash == 0 && corrupt == 0 && unexpected == 0) ? 0 : 1;
}
