/* Thin, ABI-preserving wrappers for the C11/C23 <stdatomic.h> operations.
 *
 * Every function is a pass-through to the host compiler's __atomic_* builtins
 * operating on caller-owned plain storage.  This file is C99-clean: it must
 * compile with -std=c99 -Wall -Wextra -Wpedantic as well as -std=c11.  Nothing
 * here includes <stdatomic.h> or exposes an _Atomic type.
 *
 * Integration note for the build owner: add
 *   libc/src/stdlib/atomic_wrappers.c
 * to LIBC_SOURCES in libc/CMakeLists.txt and to the loop in
 * tests/libc/build_libc.sh so these symbols enter libcplus_hardened.a.
 */
#include "../../install-include/cplus_stdatomic.h"

#define CPLUS_SEQ CPLUS_MEMORY_ORDER_SEQ_CST
#define CPLUS_REL CPLUS_MEMORY_ORDER_RELAXED

#define CPLUS_DEFINE_INTEGER(T, SUF)                                                                 \
void cplus_atomic_init_##SUF(T *obj, T desired) {                                                    \
    __atomic_store_n(obj, desired, CPLUS_REL);                                                       \
}                                                                                                    \
int cplus_atomic_is_lock_free_##SUF(const T *obj) {                                                  \
    return __atomic_is_lock_free(sizeof(T), (void *)obj) ? 1 : 0;                                    \
}                                                                                                    \
void cplus_atomic_store_##SUF(T *obj, T desired) {                                                   \
    __atomic_store_n(obj, desired, CPLUS_SEQ);                                                       \
}                                                                                                    \
void cplus_atomic_store_explicit_##SUF(T *obj, T desired, int order) {                              \
    __atomic_store_n(obj, desired, order);                                                           \
}                                                                                                    \
T cplus_atomic_load_##SUF(const T *obj) {                                                            \
    return __atomic_load_n(obj, CPLUS_SEQ);                                                          \
}                                                                                                    \
T cplus_atomic_load_explicit_##SUF(const T *obj, int order) {                                       \
    return __atomic_load_n(obj, order);                                                              \
}                                                                                                    \
T cplus_atomic_exchange_##SUF(T *obj, T desired) {                                                   \
    return __atomic_exchange_n(obj, desired, CPLUS_SEQ);                                             \
}                                                                                                    \
T cplus_atomic_exchange_explicit_##SUF(T *obj, T desired, int order) {                              \
    return __atomic_exchange_n(obj, desired, order);                                                 \
}                                                                                                    \
int cplus_atomic_compare_exchange_strong_##SUF(T *obj, T *expected, T desired) {                    \
    return __atomic_compare_exchange_n(obj, expected, desired, 0, CPLUS_SEQ, CPLUS_SEQ) ? 1 : 0;     \
}                                                                                                    \
int cplus_atomic_compare_exchange_strong_explicit_##SUF(T *obj, T *expected, T desired,             \
                                                        int success, int failure) {                \
    return __atomic_compare_exchange_n(obj, expected, desired, 0, success, failure) ? 1 : 0;        \
}                                                                                                    \
int cplus_atomic_compare_exchange_weak_##SUF(T *obj, T *expected, T desired) {                      \
    return __atomic_compare_exchange_n(obj, expected, desired, 1, CPLUS_SEQ, CPLUS_SEQ) ? 1 : 0;     \
}                                                                                                    \
int cplus_atomic_compare_exchange_weak_explicit_##SUF(T *obj, T *expected, T desired,               \
                                                      int success, int failure) {                  \
    return __atomic_compare_exchange_n(obj, expected, desired, 1, success, failure) ? 1 : 0;        \
}                                                                                                    \
T cplus_atomic_fetch_add_##SUF(T *obj, T operand) {                                                  \
    return __atomic_fetch_add(obj, operand, CPLUS_SEQ);                                              \
}                                                                                                    \
T cplus_atomic_fetch_add_explicit_##SUF(T *obj, T operand, int order) {                             \
    return __atomic_fetch_add(obj, operand, order);                                                  \
}                                                                                                    \
T cplus_atomic_fetch_sub_##SUF(T *obj, T operand) {                                                  \
    return __atomic_fetch_sub(obj, operand, CPLUS_SEQ);                                              \
}                                                                                                    \
T cplus_atomic_fetch_sub_explicit_##SUF(T *obj, T operand, int order) {                             \
    return __atomic_fetch_sub(obj, operand, order);                                                  \
}                                                                                                    \
T cplus_atomic_fetch_or_##SUF(T *obj, T operand) {                                                   \
    return __atomic_fetch_or(obj, operand, CPLUS_SEQ);                                               \
}                                                                                                    \
T cplus_atomic_fetch_or_explicit_##SUF(T *obj, T operand, int order) {                              \
    return __atomic_fetch_or(obj, operand, order);                                                   \
}                                                                                                    \
T cplus_atomic_fetch_xor_##SUF(T *obj, T operand) {                                                  \
    return __atomic_fetch_xor(obj, operand, CPLUS_SEQ);                                              \
}                                                                                                    \
T cplus_atomic_fetch_xor_explicit_##SUF(T *obj, T operand, int order) {                             \
    return __atomic_fetch_xor(obj, operand, order);                                                  \
}                                                                                                    \
T cplus_atomic_fetch_and_##SUF(T *obj, T operand) {                                                  \
    return __atomic_fetch_and(obj, operand, CPLUS_SEQ);                                              \
}                                                                                                    \
T cplus_atomic_fetch_and_explicit_##SUF(T *obj, T operand, int order) {                             \
    return __atomic_fetch_and(obj, operand, order);                                                  \
}                                                                                                    \
T cplus_atomic_fetch_nand_##SUF(T *obj, T operand) {                                                \
    return __atomic_fetch_nand(obj, operand, CPLUS_SEQ);                                             \
}                                                                                                    \
T cplus_atomic_fetch_nand_explicit_##SUF(T *obj, T operand, int order) {                            \
    return __atomic_fetch_nand(obj, operand, order);                                                 \
}                                                                                                    \
T cplus_kill_dependency_##SUF(T value) { return value; }

#define CPLUS_DEFINE_PTR_ATOMICS(SUF)                                                                \
void cplus_atomic_init_##SUF(void **obj, void *desired) {                                           \
    __atomic_store_n(obj, desired, CPLUS_REL);                                                       \
}                                                                                                    \
int cplus_atomic_is_lock_free_##SUF(void *const *obj) {                                             \
    return __atomic_is_lock_free(sizeof(void *), (void *)obj) ? 1 : 0;                              \
}                                                                                                    \
void cplus_atomic_store_##SUF(void **obj, void *desired) {                                          \
    __atomic_store_n(obj, desired, CPLUS_SEQ);                                                       \
}                                                                                                    \
void cplus_atomic_store_explicit_##SUF(void **obj, void *desired, int order) {                      \
    __atomic_store_n(obj, desired, order);                                                           \
}                                                                                                    \
void *cplus_atomic_load_##SUF(void *const *obj) {                                                    \
    return __atomic_load_n(obj, CPLUS_SEQ);                                                          \
}                                                                                                    \
void *cplus_atomic_load_explicit_##SUF(void *const *obj, int order) {                               \
    return __atomic_load_n(obj, order);                                                              \
}                                                                                                    \
void *cplus_atomic_exchange_##SUF(void **obj, void *desired) {                                      \
    return __atomic_exchange_n(obj, desired, CPLUS_SEQ);                                             \
}                                                                                                    \
void *cplus_atomic_exchange_explicit_##SUF(void **obj, void *desired, int order) {                  \
    return __atomic_exchange_n(obj, desired, order);                                                 \
}                                                                                                    \
int cplus_atomic_compare_exchange_strong_##SUF(void **obj, void **expected, void *desired) {        \
    return __atomic_compare_exchange_n(obj, expected, desired, 0, CPLUS_SEQ, CPLUS_SEQ) ? 1 : 0;    \
}                                                                                                    \
int cplus_atomic_compare_exchange_strong_explicit_##SUF(void **obj, void **expected,                \
                                                        void *desired, int success, int failure) {  \
    return __atomic_compare_exchange_n(obj, expected, desired, 0, success, failure) ? 1 : 0;        \
}                                                                                                    \
int cplus_atomic_compare_exchange_weak_##SUF(void **obj, void **expected, void *desired) {          \
    return __atomic_compare_exchange_n(obj, expected, desired, 1, CPLUS_SEQ, CPLUS_SEQ) ? 1 : 0;    \
}                                                                                                    \
int cplus_atomic_compare_exchange_weak_explicit_##SUF(void **obj, void **expected,                  \
                                                      void *desired, int success, int failure) {    \
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
    return __atomic_test_and_set(&obj->__cplus_value, CPLUS_SEQ) ? 1 : 0;
}
int cplus_atomic_flag_test_and_set_explicit(cplus_atomic_flag *obj, int order) {
    return __atomic_test_and_set(&obj->__cplus_value, order) ? 1 : 0;
}
void cplus_atomic_flag_clear(cplus_atomic_flag *obj) {
    __atomic_clear(&obj->__cplus_value, CPLUS_SEQ);
}
void cplus_atomic_flag_clear_explicit(cplus_atomic_flag *obj, int order) {
    __atomic_clear(&obj->__cplus_value, order);
}

void cplus_atomic_thread_fence(int order) { __atomic_thread_fence(order); }
void cplus_atomic_signal_fence(int order) { __atomic_signal_fence(order); }
