/* Tests for the namespaced <stdatomic.h> C ABI surface.
 *
 * Compiled C99-clean (no _Atomic / <stdatomic.h>): this proves the public
 * header and wrappers are usable from a C99 consumer as well as C11.  Every
 * exported cplus_* function is called at least once (linkage coverage), then
 * representative semantics and a bounded pthread concurrency smoke test are
 * checked.  Run under `timeout 30` in CI.
 */
#define _POSIX_C_SOURCE 200112L
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>

#include "install-include/cplus_stdatomic.h"

#define CHECK(c)                                                           \
    do {                                                                   \
        if (!(c)) {                                                        \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c);   \
            return 1;                                                      \
        }                                                                  \
    } while (0)

/* Call all 25 per-integer-type functions; verify the core semantics. */
#define EXERCISE_INTEGER(T, suf)                                                              \
    do {                                                                                      \
        T v = (T)0, r, exp;                                                                   \
        int o = CPLUS_MEMORY_ORDER_RELAXED, cas;                                              \
        (void)cplus_atomic_is_lock_free_##suf(&v);                                            \
        cplus_atomic_init_##suf(&v, (T)5);                                                    \
        CHECK(cplus_atomic_load_##suf(&v) == (T)5);                                           \
        CHECK(cplus_atomic_load_explicit_##suf(&v, o) == (T)5);                               \
        cplus_atomic_store_##suf(&v, (T)9);                                                   \
        CHECK(cplus_atomic_load_##suf(&v) == (T)9);                                           \
        cplus_atomic_store_explicit_##suf(&v, (T)7, o);                                       \
        r = cplus_atomic_exchange_##suf(&v, (T)3);                                            \
        CHECK(r == (T)7 && cplus_atomic_load_##suf(&v) == (T)3);                              \
        r = cplus_atomic_exchange_explicit_##suf(&v, (T)4, o);                                \
        CHECK(r == (T)3 && cplus_atomic_load_##suf(&v) == (T)4);                              \
        exp = (T)4;                                                                            \
        cas = cplus_atomic_compare_exchange_strong_##suf(&v, &exp, (T)6);                     \
        CHECK(cas == 1 && cplus_atomic_load_##suf(&v) == (T)6);                               \
        exp = (T)6;                                                                            \
        cas = cplus_atomic_compare_exchange_strong_explicit_##suf(                            \
            &v, &exp, (T)8, CPLUS_MEMORY_ORDER_SEQ_CST, CPLUS_MEMORY_ORDER_RELAXED);          \
        CHECK(cas == 1 && cplus_atomic_load_##suf(&v) == (T)8);                               \
        exp = (T)99;                                                                           \
        cas = cplus_atomic_compare_exchange_weak_##suf(&v, &exp, (T)1);                       \
        CHECK(cas == 0 && exp == (T)8);                                                        \
        exp = (T)8;                                                                            \
        cas = cplus_atomic_compare_exchange_weak_explicit_##suf(                              \
            &v, &exp, (T)1, CPLUS_MEMORY_ORDER_SEQ_CST, CPLUS_MEMORY_ORDER_RELAXED);          \
        CHECK(cas == 1 && cplus_atomic_load_##suf(&v) == (T)1);                               \
        cplus_atomic_store_##suf(&v, (T)10);                                                  \
        CHECK(cplus_atomic_fetch_add_##suf(&v, (T)2) == (T)10);                               \
        CHECK(cplus_atomic_load_##suf(&v) == (T)12);                                          \
        CHECK(cplus_atomic_fetch_add_explicit_##suf(&v, (T)1, o) == (T)12);                   \
        CHECK(cplus_atomic_fetch_sub_##suf(&v, (T)3) == (T)13);                               \
        CHECK(cplus_atomic_load_##suf(&v) == (T)10);                                          \
        CHECK(cplus_atomic_fetch_sub_explicit_##suf(&v, (T)1, o) == (T)10);                   \
        cplus_atomic_store_##suf(&v, (T)12);                                                  \
        CHECK(cplus_atomic_fetch_or_##suf(&v, (T)3) == (T)12);                                \
        CHECK(cplus_atomic_load_##suf(&v) == (T)15);                                          \
        CHECK(cplus_atomic_fetch_or_explicit_##suf(&v, (T)0, o) == (T)15);                    \
        CHECK(cplus_atomic_fetch_xor_##suf(&v, (T)1) == (T)15);                               \
        CHECK(cplus_atomic_load_##suf(&v) == (T)14);                                          \
        CHECK(cplus_atomic_fetch_xor_explicit_##suf(&v, (T)0, o) == (T)14);                   \
        CHECK(cplus_atomic_fetch_and_##suf(&v, (T)10) == (T)14);                              \
        CHECK(cplus_atomic_load_##suf(&v) == (T)10);                                          \
        CHECK(cplus_atomic_fetch_and_explicit_##suf(&v, (T)15, o) == (T)10);                  \
        cplus_atomic_store_##suf(&v, (T)10);                                                  \
        CHECK(cplus_atomic_fetch_nand_##suf(&v, (T)15) == (T)10);                              \
        cplus_atomic_store_##suf(&v, (T)10);                                                  \
        CHECK(cplus_atomic_fetch_nand_explicit_##suf(&v, (T)15, o) == (T)10);                  \
        CHECK(cplus_kill_dependency_##suf((T)77) == (T)77);                                   \
    } while (0)

/* Call all 13 void* functions.  Arithmetic fetch_* is intentionally absent. */
#define EXERCISE_PTR()                                                                        \
    do {                                                                                      \
        void *p = NULL, *r, *exp;                                                             \
        int o = CPLUS_MEMORY_ORDER_RELAXED, cas;                                              \
        (void)cplus_atomic_is_lock_free_ptr(&p);                                              \
        cplus_atomic_init_ptr(&p, (void *)(uintptr_t)0x10);                                   \
        CHECK(cplus_atomic_load_ptr(&p) == (void *)(uintptr_t)0x10);                           \
        CHECK(cplus_atomic_load_explicit_ptr(&p, o) == (void *)(uintptr_t)0x10);               \
        cplus_atomic_store_ptr(&p, (void *)(uintptr_t)0x20);                                  \
        CHECK(cplus_atomic_load_ptr(&p) == (void *)(uintptr_t)0x20);                           \
        cplus_atomic_store_explicit_ptr(&p, (void *)(uintptr_t)0x30, o);                      \
        r = cplus_atomic_exchange_ptr(&p, (void *)(uintptr_t)0x40);                           \
        CHECK(r == (void *)(uintptr_t)0x30);                                                   \
        r = cplus_atomic_exchange_explicit_ptr(&p, (void *)(uintptr_t)0x50, o);               \
        CHECK(r == (void *)(uintptr_t)0x40);                                                   \
        exp = (void *)(uintptr_t)0x50;                                                         \
        cas = cplus_atomic_compare_exchange_strong_ptr(&p, &exp, (void *)(uintptr_t)0x60);    \
        CHECK(cas == 1 && cplus_atomic_load_ptr(&p) == (void *)(uintptr_t)0x60);              \
        exp = (void *)(uintptr_t)0x60;                                                         \
        cas = cplus_atomic_compare_exchange_strong_explicit_ptr(                              \
            &p, &exp, (void *)(uintptr_t)0x70, CPLUS_MEMORY_ORDER_SEQ_CST,                    \
            CPLUS_MEMORY_ORDER_RELAXED);                                                      \
        CHECK(cas == 1 && cplus_atomic_load_ptr(&p) == (void *)(uintptr_t)0x70);              \
        exp = (void *)(uintptr_t)0x00;                                                         \
        cas = cplus_atomic_compare_exchange_weak_ptr(&p, &exp, (void *)(uintptr_t)0x80);      \
        CHECK(cas == 0 && exp == (void *)(uintptr_t)0x70);                                    \
        exp = (void *)(uintptr_t)0x70;                                                         \
        cas = cplus_atomic_compare_exchange_weak_explicit_ptr(                                \
            &p, &exp, (void *)(uintptr_t)0x80, CPLUS_MEMORY_ORDER_SEQ_CST,                    \
            CPLUS_MEMORY_ORDER_RELAXED);                                                      \
        CHECK(cas == 1 && cplus_atomic_load_ptr(&p) == (void *)(uintptr_t)0x80);              \
        CHECK(cplus_kill_dependency_ptr(p) == (void *)(uintptr_t)0x80);                       \
    } while (0)

static int test_scalar_and_flag(void) {
    EXERCISE_INTEGER(int, int);
    EXERCISE_INTEGER(unsigned int, uint);
    EXERCISE_INTEGER(long, long);
    EXERCISE_INTEGER(unsigned long, ulong);
    EXERCISE_INTEGER(long long, llong);
    EXERCISE_INTEGER(unsigned long long, ullong);
    EXERCISE_PTR();

    {
        cplus_atomic_flag flag = CPLUS_ATOMIC_FLAG_INIT;
        CHECK(cplus_atomic_flag_test_and_set(&flag) == 0);
        CHECK(cplus_atomic_flag_test_and_set(&flag) == 1);
        cplus_atomic_flag_clear(&flag);
        CHECK(cplus_atomic_flag_test_and_set_explicit(&flag, CPLUS_MEMORY_ORDER_ACQ_REL) == 0);
        cplus_atomic_flag_clear_explicit(&flag, CPLUS_MEMORY_ORDER_RELEASE);
        CHECK(cplus_atomic_flag_test_and_set(&flag) == 0);
    }
    cplus_atomic_thread_fence(CPLUS_MEMORY_ORDER_SEQ_CST);
    cplus_atomic_signal_fence(CPLUS_MEMORY_ORDER_SEQ_CST);
    return 0;
}

/* ---- Bounded concurrency smoke test ----------------------------------- */
#define NTHREADS 4
#define NITER    20000
static int shared_counter;
static int cas_counter;
static int flag_counter;
static cplus_atomic_flag spin = CPLUS_ATOMIC_FLAG_INIT;

static void *worker(void *arg) {
    int i;
    (void)arg;
    for (i = 0; i < NITER; ++i) {
        (void)cplus_atomic_fetch_add_int(&shared_counter, 1);
    }
    for (;;) {
        int expected = cplus_atomic_load_explicit_int(&cas_counter, CPLUS_MEMORY_ORDER_RELAXED);
        if (cplus_atomic_compare_exchange_weak_explicit_int(
                &cas_counter, &expected, expected + 1,
                CPLUS_MEMORY_ORDER_ACQ_REL, CPLUS_MEMORY_ORDER_RELAXED)) {
            break;
        }
    }
    while (cplus_atomic_flag_test_and_set_explicit(&spin, CPLUS_MEMORY_ORDER_ACQUIRE)) {
        /* bounded spin: the critical section below is a single increment */
    }
    flag_counter += 1;
    cplus_atomic_flag_clear_explicit(&spin, CPLUS_MEMORY_ORDER_RELEASE);
    return NULL;
}

static int test_concurrency(void) {
    pthread_t th[NTHREADS];
    int i;
    for (i = 0; i < NTHREADS; ++i) {
        CHECK(pthread_create(&th[i], NULL, worker, NULL) == 0);
    }
    for (i = 0; i < NTHREADS; ++i) {
        CHECK(pthread_join(th[i], NULL) == 0);
    }
    CHECK(cplus_atomic_load_int(&shared_counter) == NTHREADS * NITER);
    CHECK(cplus_atomic_load_int(&cas_counter) == NTHREADS);
    CHECK(flag_counter == NTHREADS);
    return 0;
}

int main(void) {
    if (test_scalar_and_flag() != 0 || test_concurrency() != 0) {
        return 1;
    }
    printf("cplus_atomic: all closed wrappers and concurrency smoke test passed\n");
    return 0;
}
