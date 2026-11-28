/* Thin, ABI-preserving wrappers for the C11/C23 <stdatomic.h> operations.
 *
 * Every function is a pass-through to the host compiler's __atomic_* builtins
 * operating on caller-owned plain storage.  This file is C99-clean: it must
 * compile with -std=c99 -Wall -Wextra -Wpedantic as well as -std=c11.  Nothing
 * here includes <stdatomic.h> or exposes an _Atomic type.
 *
 * Safety (issue #176): every wrapper validates its pointer arguments and its
 * memory-order arguments before reaching a builtin.  NULL is never forwarded
 * into a raw __atomic_* operation, and an out-of-range or operation-illegal
 * order is rejected with a deterministic diagnostic.  The diagnostic uses the
 * same exit codes as the C+ runtime's cplus_exit_code (102 NULL pointer,
 * 108 unsupported operation) so a libc-only link behaves consistently without
 * pulling in runtime/cplus_runtime.c.  Valid calls keep the exact original
 * ABI and semantics.
 *
 * Integration note for the build owner: add
 *   libc/src/stdlib/atomic_wrappers.c
 * to LIBC_SOURCES in libc/CMakeLists.txt and to the loop in
 * tests/libc/build_libc.sh so these symbols enter libcplus_hardened.a.
 */
#include "../../install-include/cplus_stdatomic.h"

#include <stdio.h>
#include <stdlib.h>

#define CPLUS_SEQ CPLUS_MEMORY_ORDER_SEQ_CST
#define CPLUS_REL CPLUS_MEMORY_ORDER_RELAXED

#define CPLUS_ATOMIC_EXIT_NULL  102
#define CPLUS_ATOMIC_EXIT_ORDER 108

static void cplus_atomic_diag(const char *api, const char *detail, int code) {
    fprintf(stderr, "C+ libc atomic error: %s: %s\n", api, detail);
    fflush(stderr); /* _Exit does not flush */
    _Exit(code);
}

/* ISO C11 7.17: orders are 0..5.  Loads and test_and_set (read-modify-write
 * with no release semantics) forbid release/acq_rel; stores and flag_clear
 * forbid consume/acquire/acq_rel; CAS failure must be a legal load order and
 * no stronger than success. */
static int cplus_atomic_order_ok(int order) {
    return order >= (int)CPLUS_MEMORY_ORDER_RELAXED &&
           order <= (int)CPLUS_MEMORY_ORDER_SEQ_CST;
}
static int cplus_atomic_load_order_ok(int order) {
    return cplus_atomic_order_ok(order) &&
           order != (int)CPLUS_MEMORY_ORDER_RELEASE &&
           order != (int)CPLUS_MEMORY_ORDER_ACQ_REL;
}
static int cplus_atomic_store_order_ok(int order) {
    return cplus_atomic_order_ok(order) &&
           order != (int)CPLUS_MEMORY_ORDER_CONSUME &&
           order != (int)CPLUS_MEMORY_ORDER_ACQUIRE &&
           order != (int)CPLUS_MEMORY_ORDER_ACQ_REL;
}
static int cplus_atomic_cas_ok(int success, int failure) {
    return cplus_atomic_order_ok(success) &&
           cplus_atomic_load_order_ok(failure) &&
           failure <= success; /* no stronger than success */
}

#define CPLUS_ATOMIC_REQUIRE_OBJ(p, api)                                       \
    do {                                                                       \
        if ((p) == NULL)                                                       \
            cplus_atomic_diag((api), "null object pointer",                    \
                              CPLUS_ATOMIC_EXIT_NULL);                         \
    } while (0)

#define CPLUS_ATOMIC_REQUIRE_EXPECTED(p, api)                                  \
    do {                                                                       \
        if ((p) == NULL)                                                       \
            cplus_atomic_diag((api), "null expected pointer",                  \
                              CPLUS_ATOMIC_EXIT_NULL);                         \
    } while (0)

#define CPLUS_ATOMIC_REQUIRE_ORDER(o, okfn, api)                               \
    do {                                                                       \
        if (!(okfn(o)))                                                        \
            cplus_atomic_diag((api), "invalid memory order",                   \
                              CPLUS_ATOMIC_EXIT_ORDER);                        \
    } while (0)

#define CPLUS_ATOMIC_REQUIRE_CAS(s, f, api)                                    \
    do {                                                                       \
        if (!cplus_atomic_cas_ok((s), (f)))                                    \
            cplus_atomic_diag((api), "invalid memory order",                   \
                              CPLUS_ATOMIC_EXIT_ORDER);                        \
    } while (0)

#define CPLUS_DEFINE_INTEGER(T, SUF)                                                                 \
void cplus_atomic_init_##SUF(T *obj, T desired) {                                                    \
    CPLUS_ATOMIC_REQUIRE_OBJ(obj, "cplus_atomic_init_" #SUF);                                        \
    __atomic_store_n(obj, desired, CPLUS_REL);                                                       \
}                                                                                                    \
int cplus_atomic_is_lock_free_##SUF(const T *obj) {                                                  \
    CPLUS_ATOMIC_REQUIRE_OBJ(obj, "cplus_atomic_is_lock_free_" #SUF);                                \
    return __atomic_is_lock_free(sizeof(T), (void *)obj) ? 1 : 0;                                    \
}                                                                                                    \
void cplus_atomic_store_##SUF(T *obj, T desired) {                                                   \
    CPLUS_ATOMIC_REQUIRE_OBJ(obj, "cplus_atomic_store_" #SUF);                                       \
    __atomic_store_n(obj, desired, CPLUS_SEQ);                                                       \
}                                                                                                    \
void cplus_atomic_store_explicit_##SUF(T *obj, T desired, int order) {                              \
    CPLUS_ATOMIC_REQUIRE_OBJ(obj, "cplus_atomic_store_explicit_" #SUF);                              \
    CPLUS_ATOMIC_REQUIRE_ORDER(order, cplus_atomic_store_order_ok,                                   \
                               "cplus_atomic_store_explicit_" #SUF);                                \
    __atomic_store_n(obj, desired, order);                                                           \
}                                                                                                    \
T cplus_atomic_load_##SUF(const T *obj) {                                                            \
    CPLUS_ATOMIC_REQUIRE_OBJ(obj, "cplus_atomic_load_" #SUF);                                        \
    return __atomic_load_n(obj, CPLUS_SEQ);                                                          \
}                                                                                                    \
T cplus_atomic_load_explicit_##SUF(const T *obj, int order) {                                       \
    CPLUS_ATOMIC_REQUIRE_OBJ(obj, "cplus_atomic_load_explicit_" #SUF);                               \
    CPLUS_ATOMIC_REQUIRE_ORDER(order, cplus_atomic_load_order_ok,                                    \
                               "cplus_atomic_load_explicit_" #SUF);                                 \
    return __atomic_load_n(obj, order);                                                              \
}                                                                                                    \
T cplus_atomic_exchange_##SUF(T *obj, T desired) {                                                  \
    CPLUS_ATOMIC_REQUIRE_OBJ(obj, "cplus_atomic_exchange_" #SUF);                                    \
    return __atomic_exchange_n(obj, desired, CPLUS_SEQ);                                             \
}                                                                                                    \
T cplus_atomic_exchange_explicit_##SUF(T *obj, T desired, int order) {                              \
    CPLUS_ATOMIC_REQUIRE_OBJ(obj, "cplus_atomic_exchange_explicit_" #SUF);                           \
    CPLUS_ATOMIC_REQUIRE_ORDER(order, cplus_atomic_order_ok,                                         \
                               "cplus_atomic_exchange_explicit_" #SUF);                             \
    return __atomic_exchange_n(obj, desired, order);                                                 \
}                                                                                                    \
int cplus_atomic_compare_exchange_strong_##SUF(T *obj, T *expected, T desired) {                    \
    CPLUS_ATOMIC_REQUIRE_OBJ(obj, "cplus_atomic_compare_exchange_strong_" #SUF);                     \
    CPLUS_ATOMIC_REQUIRE_EXPECTED(expected,                                                          \
                                  "cplus_atomic_compare_exchange_strong_" #SUF);                    \
    return __atomic_compare_exchange_n(obj, expected, desired, 0, CPLUS_SEQ, CPLUS_SEQ) ? 1 : 0;     \
}                                                                                                    \
int cplus_atomic_compare_exchange_strong_explicit_##SUF(T *obj, T *expected, T desired,             \
                                                         int success, int failure) {                \
    CPLUS_ATOMIC_REQUIRE_OBJ(obj,                                                                    \
                             "cplus_atomic_compare_exchange_strong_explicit_" #SUF);                \
    CPLUS_ATOMIC_REQUIRE_EXPECTED(expected,                                                          \
                                  "cplus_atomic_compare_exchange_strong_explicit_" #SUF);           \
    CPLUS_ATOMIC_REQUIRE_CAS(success, failure,                                                       \
                             "cplus_atomic_compare_exchange_strong_explicit_" #SUF);                \
    return __atomic_compare_exchange_n(obj, expected, desired, 0, success, failure) ? 1 : 0;        \
}                                                                                                    \
int cplus_atomic_compare_exchange_weak_##SUF(T *obj, T *expected, T desired) {                      \
    CPLUS_ATOMIC_REQUIRE_OBJ(obj, "cplus_atomic_compare_exchange_weak_" #SUF);                       \
    CPLUS_ATOMIC_REQUIRE_EXPECTED(expected,                                                          \
                                  "cplus_atomic_compare_exchange_weak_" #SUF);                      \
    return __atomic_compare_exchange_n(obj, expected, desired, 1, CPLUS_SEQ, CPLUS_SEQ) ? 1 : 0;     \
}                                                                                                    \
int cplus_atomic_compare_exchange_weak_explicit_##SUF(T *obj, T *expected, T desired,               \
                                                       int success, int failure) {                  \
    CPLUS_ATOMIC_REQUIRE_OBJ(obj,                                                                    \
                             "cplus_atomic_compare_exchange_weak_explicit_" #SUF);                  \
    CPLUS_ATOMIC_REQUIRE_EXPECTED(expected,                                                          \
                                  "cplus_atomic_compare_exchange_weak_explicit_" #SUF);             \
    CPLUS_ATOMIC_REQUIRE_CAS(success, failure,                                                       \
                             "cplus_atomic_compare_exchange_weak_explicit_" #SUF);                  \
    return __atomic_compare_exchange_n(obj, expected, desired, 1, success, failure) ? 1 : 0;        \
}                                                                                                    \
T cplus_atomic_fetch_add_##SUF(T *obj, T operand) {                                                  \
    CPLUS_ATOMIC_REQUIRE_OBJ(obj, "cplus_atomic_fetch_add_" #SUF);                                   \
    return __atomic_fetch_add(obj, operand, CPLUS_SEQ);                                              \
}                                                                                                    \
T cplus_atomic_fetch_add_explicit_##SUF(T *obj, T operand, int order) {                             \
    CPLUS_ATOMIC_REQUIRE_OBJ(obj, "cplus_atomic_fetch_add_explicit_" #SUF);                          \
    CPLUS_ATOMIC_REQUIRE_ORDER(order, cplus_atomic_order_ok,                                         \
                               "cplus_atomic_fetch_add_explicit_" #SUF);                            \
    return __atomic_fetch_add(obj, operand, order);                                                  \
}                                                                                                    \
T cplus_atomic_fetch_sub_##SUF(T *obj, T operand) {                                                  \
    CPLUS_ATOMIC_REQUIRE_OBJ(obj, "cplus_atomic_fetch_sub_" #SUF);                                   \
    return __atomic_fetch_sub(obj, operand, CPLUS_SEQ);                                              \
}                                                                                                    \
T cplus_atomic_fetch_sub_explicit_##SUF(T *obj, T operand, int order) {                             \
    CPLUS_ATOMIC_REQUIRE_OBJ(obj, "cplus_atomic_fetch_sub_explicit_" #SUF);                          \
    CPLUS_ATOMIC_REQUIRE_ORDER(order, cplus_atomic_order_ok,                                         \
                               "cplus_atomic_fetch_sub_explicit_" #SUF);                            \
    return __atomic_fetch_sub(obj, operand, order);                                                  \
}                                                                                                    \
T cplus_atomic_fetch_or_##SUF(T *obj, T operand) {                                                   \
    CPLUS_ATOMIC_REQUIRE_OBJ(obj, "cplus_atomic_fetch_or_" #SUF);                                    \
    return __atomic_fetch_or(obj, operand, CPLUS_SEQ);                                               \
}                                                                                                    \
T cplus_atomic_fetch_or_explicit_##SUF(T *obj, T operand, int order) {                              \
    CPLUS_ATOMIC_REQUIRE_OBJ(obj, "cplus_atomic_fetch_or_explicit_" #SUF);                           \
    CPLUS_ATOMIC_REQUIRE_ORDER(order, cplus_atomic_order_ok,                                         \
                               "cplus_atomic_fetch_or_explicit_" #SUF);                             \
    return __atomic_fetch_or(obj, operand, order);                                                   \
}                                                                                                    \
T cplus_atomic_fetch_xor_##SUF(T *obj, T operand) {                                                  \
    CPLUS_ATOMIC_REQUIRE_OBJ(obj, "cplus_atomic_fetch_xor_" #SUF);                                   \
    return __atomic_fetch_xor(obj, operand, CPLUS_SEQ);                                              \
}                                                                                                    \
T cplus_atomic_fetch_xor_explicit_##SUF(T *obj, T operand, int order) {                             \
    CPLUS_ATOMIC_REQUIRE_OBJ(obj, "cplus_atomic_fetch_xor_explicit_" #SUF);                          \
    CPLUS_ATOMIC_REQUIRE_ORDER(order, cplus_atomic_order_ok,                                         \
                               "cplus_atomic_fetch_xor_explicit_" #SUF);                            \
    return __atomic_fetch_xor(obj, operand, order);                                                  \
}                                                                                                    \
T cplus_atomic_fetch_and_##SUF(T *obj, T operand) {                                                  \
    CPLUS_ATOMIC_REQUIRE_OBJ(obj, "cplus_atomic_fetch_and_" #SUF);                                   \
    return __atomic_fetch_and(obj, operand, CPLUS_SEQ);                                              \
}                                                                                                    \
T cplus_atomic_fetch_and_explicit_##SUF(T *obj, T operand, int order) {                             \
    CPLUS_ATOMIC_REQUIRE_OBJ(obj, "cplus_atomic_fetch_and_explicit_" #SUF);                          \
    CPLUS_ATOMIC_REQUIRE_ORDER(order, cplus_atomic_order_ok,                                         \
                               "cplus_atomic_fetch_and_explicit_" #SUF);                            \
    return __atomic_fetch_and(obj, operand, order);                                                  \
}                                                                                                    \
T cplus_atomic_fetch_nand_##SUF(T *obj, T operand) {                                                 \
    CPLUS_ATOMIC_REQUIRE_OBJ(obj, "cplus_atomic_fetch_nand_" #SUF);                                  \
    return __atomic_fetch_nand(obj, operand, CPLUS_SEQ);                                             \
}                                                                                                    \
T cplus_atomic_fetch_nand_explicit_##SUF(T *obj, T operand, int order) {                            \
    CPLUS_ATOMIC_REQUIRE_OBJ(obj, "cplus_atomic_fetch_nand_explicit_" #SUF);                         \
    CPLUS_ATOMIC_REQUIRE_ORDER(order, cplus_atomic_order_ok,                                         \
                               "cplus_atomic_fetch_nand_explicit_" #SUF);                           \
    return __atomic_fetch_nand(obj, operand, order);                                                 \
}                                                                                                    \
T cplus_kill_dependency_##SUF(T value) { return value; }

#define CPLUS_DEFINE_PTR_ATOMICS(SUF)                                                                \
void cplus_atomic_init_##SUF(void **obj, void *desired) {                                           \
    CPLUS_ATOMIC_REQUIRE_OBJ(obj, "cplus_atomic_init_" #SUF);                                        \
    __atomic_store_n(obj, desired, CPLUS_REL);                                                       \
}                                                                                                    \
int cplus_atomic_is_lock_free_##SUF(void *const *obj) {                                             \
    CPLUS_ATOMIC_REQUIRE_OBJ(obj, "cplus_atomic_is_lock_free_" #SUF);                                \
    return __atomic_is_lock_free(sizeof(void *), (void *)obj) ? 1 : 0;                              \
}                                                                                                    \
void cplus_atomic_store_##SUF(void **obj, void *desired) {                                          \
    CPLUS_ATOMIC_REQUIRE_OBJ(obj, "cplus_atomic_store_" #SUF);                                       \
    __atomic_store_n(obj, desired, CPLUS_SEQ);                                                       \
}                                                                                                    \
void cplus_atomic_store_explicit_##SUF(void **obj, void *desired, int order) {                      \
    CPLUS_ATOMIC_REQUIRE_OBJ(obj, "cplus_atomic_store_explicit_" #SUF);                              \
    CPLUS_ATOMIC_REQUIRE_ORDER(order, cplus_atomic_store_order_ok,                                   \
                               "cplus_atomic_store_explicit_" #SUF);                                \
    __atomic_store_n(obj, desired, order);                                                           \
}                                                                                                    \
void *cplus_atomic_load_##SUF(void *const *obj) {                                                    \
    CPLUS_ATOMIC_REQUIRE_OBJ(obj, "cplus_atomic_load_" #SUF);                                        \
    return __atomic_load_n(obj, CPLUS_SEQ);                                                          \
}                                                                                                    \
void *cplus_atomic_load_explicit_##SUF(void *const *obj, int order) {                               \
    CPLUS_ATOMIC_REQUIRE_OBJ(obj, "cplus_atomic_load_explicit_" #SUF);                               \
    CPLUS_ATOMIC_REQUIRE_ORDER(order, cplus_atomic_load_order_ok,                                    \
                               "cplus_atomic_load_explicit_" #SUF);                                 \
    return __atomic_load_n(obj, order);                                                              \
}                                                                                                    \
void *cplus_atomic_exchange_##SUF(void **obj, void *desired) {                                      \
    CPLUS_ATOMIC_REQUIRE_OBJ(obj, "cplus_atomic_exchange_" #SUF);                                    \
    return __atomic_exchange_n(obj, desired, CPLUS_SEQ);                                             \
}                                                                                                    \
void *cplus_atomic_exchange_explicit_##SUF(void **obj, void *desired, int order) {                  \
    CPLUS_ATOMIC_REQUIRE_OBJ(obj, "cplus_atomic_exchange_explicit_" #SUF);                           \
    CPLUS_ATOMIC_REQUIRE_ORDER(order, cplus_atomic_order_ok,                                         \
                               "cplus_atomic_exchange_explicit_" #SUF);                             \
    return __atomic_exchange_n(obj, desired, order);                                                 \
}                                                                                                    \
int cplus_atomic_compare_exchange_strong_##SUF(void **obj, void **expected, void *desired) {        \
    CPLUS_ATOMIC_REQUIRE_OBJ(obj, "cplus_atomic_compare_exchange_strong_" #SUF);                     \
    CPLUS_ATOMIC_REQUIRE_EXPECTED(expected,                                                          \
                                  "cplus_atomic_compare_exchange_strong_" #SUF);                    \
    return __atomic_compare_exchange_n(obj, expected, desired, 0, CPLUS_SEQ, CPLUS_SEQ) ? 1 : 0;    \
}                                                                                                    \
int cplus_atomic_compare_exchange_strong_explicit_##SUF(void **obj, void **expected,                \
                                                         void *desired, int success, int failure) {  \
    CPLUS_ATOMIC_REQUIRE_OBJ(obj,                                                                    \
                             "cplus_atomic_compare_exchange_strong_explicit_" #SUF);                \
    CPLUS_ATOMIC_REQUIRE_EXPECTED(expected,                                                          \
                                  "cplus_atomic_compare_exchange_strong_explicit_" #SUF);           \
    CPLUS_ATOMIC_REQUIRE_CAS(success, failure,                                                       \
                             "cplus_atomic_compare_exchange_strong_explicit_" #SUF);                \
    return __atomic_compare_exchange_n(obj, expected, desired, 0, success, failure) ? 1 : 0;        \
}                                                                                                    \
int cplus_atomic_compare_exchange_weak_##SUF(void **obj, void **expected, void *desired) {          \
    CPLUS_ATOMIC_REQUIRE_OBJ(obj, "cplus_atomic_compare_exchange_weak_" #SUF);                       \
    CPLUS_ATOMIC_REQUIRE_EXPECTED(expected,                                                          \
                                  "cplus_atomic_compare_exchange_weak_" #SUF);                      \
    return __atomic_compare_exchange_n(obj, expected, desired, 1, CPLUS_SEQ, CPLUS_SEQ) ? 1 : 0;    \
}                                                                                                    \
int cplus_atomic_compare_exchange_weak_explicit_##SUF(void **obj, void **expected,                  \
                                                       void *desired, int success, int failure) {    \
    CPLUS_ATOMIC_REQUIRE_OBJ(obj,                                                                    \
                             "cplus_atomic_compare_exchange_weak_explicit_" #SUF);                  \
    CPLUS_ATOMIC_REQUIRE_EXPECTED(expected,                                                          \
                                  "cplus_atomic_compare_exchange_weak_explicit_" #SUF);             \
    CPLUS_ATOMIC_REQUIRE_CAS(success, failure,                                                       \
                             "cplus_atomic_compare_exchange_weak_explicit_" #SUF);                  \
    return __atomic_compare_exchange_n(obj, expected, desired, 1, success, failure) ? 1 : 0;        \
}                                                                                                    \
void *cplus_kill_dependency_##SUF(void *value) { return value; }

CPLUS_DEFINE_INTEGER(int, int)
CPLUS_DEFINE_INTEGER(unsigned int, uint)
CPLUS_DEFINE_INTEGER(long, long)
CPLUS_DEFINE_INTEGER(unsigned long, ulong)
CPLUS_DEFINE_INTEGER(long long, llong)
CPLUS_DEFINE_INTEGER(unsigned long long, ullong)
CPLUS_DEFINE_PTR_ATOMICS(ptr)

int cplus_atomic_flag_test_and_set(cplus_atomic_flag *obj) {
    CPLUS_ATOMIC_REQUIRE_OBJ(obj, "cplus_atomic_flag_test_and_set");
    return __atomic_test_and_set(&obj->__cplus_value, CPLUS_SEQ) ? 1 : 0;
}
int cplus_atomic_flag_test_and_set_explicit(cplus_atomic_flag *obj, int order) {
    CPLUS_ATOMIC_REQUIRE_OBJ(obj, "cplus_atomic_flag_test_and_set_explicit");
    /* test_and_set is a read-modify-write: all five C11 orders, including
     * release and acq_rel, are valid (C11 7.17.8.2). */
    CPLUS_ATOMIC_REQUIRE_ORDER(order, cplus_atomic_order_ok,
                               "cplus_atomic_flag_test_and_set_explicit");
    return __atomic_test_and_set(&obj->__cplus_value, order) ? 1 : 0;
}
void cplus_atomic_flag_clear(cplus_atomic_flag *obj) {
    CPLUS_ATOMIC_REQUIRE_OBJ(obj, "cplus_atomic_flag_clear");
    __atomic_clear(&obj->__cplus_value, CPLUS_SEQ);
}
void cplus_atomic_flag_clear_explicit(cplus_atomic_flag *obj, int order) {
    CPLUS_ATOMIC_REQUIRE_OBJ(obj, "cplus_atomic_flag_clear_explicit");
    CPLUS_ATOMIC_REQUIRE_ORDER(order, cplus_atomic_store_order_ok,
                               "cplus_atomic_flag_clear_explicit");
    __atomic_clear(&obj->__cplus_value, order);
}

void cplus_atomic_thread_fence(int order) {
    CPLUS_ATOMIC_REQUIRE_ORDER(order, cplus_atomic_order_ok,
                               "cplus_atomic_thread_fence");
    __atomic_thread_fence(order);
}
void cplus_atomic_signal_fence(int order) {
    CPLUS_ATOMIC_REQUIRE_ORDER(order, cplus_atomic_order_ok,
                               "cplus_atomic_signal_fence");
    __atomic_signal_fence(order);
}
