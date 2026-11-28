/* C+ namespaced <stdatomic.h> C ABI surface.
 *
 * This is a SELF-CONTAINED, C99-compatible header.  It deliberately does NOT
 * include <stdatomic.h> and does NOT use _Atomic, _Generic or C11 keywords,
 * so it can be consumed from both -std=c99 and -std=c11 translation units.
 *
 * Every declaration below is a REAL, linker-visible C function implemented in
 * libc/src/stdlib/atomic_wrappers.c.  The macros only declare functions; they
 * are not part of the runtime API.  Callers own the storage: each wrapper
 * takes a pointer to a plain scalar (or pointer) object and performs the
 * operation with a compiler __atomic_* builtin on that storage.  No new
 * struct layout is imposed on existing headers and no host _Atomic type is
 * exposed.  Consequently the C11 type-generic macro spellings cannot be
 * reproduced verbatim; see docs/libc/STDATOMIC_CLOSURE.md for the exact
 * closed / blocked split and the recommended call pattern.
 *
 * The memory-order argument uses the C11 numeric values below so callers can
 * pass either a cplus_memory_order constant or a plain int (0..5).
 */
#ifndef CPLUS_STDATOMIC_H
#define CPLUS_STDATOMIC_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Numeric values match C11 memory_order / the compiler __atomic_* builtins. */
typedef enum cplus_memory_order {
    CPLUS_MEMORY_ORDER_RELAXED = 0,
    CPLUS_MEMORY_ORDER_CONSUME = 1,
    CPLUS_MEMORY_ORDER_ACQUIRE = 2,
    CPLUS_MEMORY_ORDER_RELEASE = 3,
    CPLUS_MEMORY_ORDER_ACQ_REL  = 4,
    CPLUS_MEMORY_ORDER_SEQ_CST  = 5
} cplus_memory_order;

/* atomic_flag storage: a single byte, matching the C11 atomic_flag ABI. */
typedef struct cplus_atomic_flag { unsigned char __cplus_value; } cplus_atomic_flag;
#define CPLUS_ATOMIC_FLAG_INIT { 0 }

/* ---- Per-type operations ------------------------------------------------
 * Suffix selects the storage type: int, uint, long, ulong, llong, ullong, ptr.
 * Load/is_lock_free take a const pointer; all mutating ops take a non-const
 * pointer.  The non-explicit forms use memory_order_seq_cst.
 */

/* storage type: int */
void cplus_atomic_init_int(int *obj, int desired);
int cplus_atomic_is_lock_free_int(const int *obj);
void cplus_atomic_store_int(int *obj, int desired);
void cplus_atomic_store_explicit_int(int *obj, int desired, int order);
int cplus_atomic_load_int(const int *obj);
int cplus_atomic_load_explicit_int(const int *obj, int order);
int cplus_atomic_exchange_int(int *obj, int desired);
int cplus_atomic_exchange_explicit_int(int *obj, int desired, int order);
int cplus_atomic_compare_exchange_strong_int(int *obj, int *expected, int desired);
int cplus_atomic_compare_exchange_strong_explicit_int(int *obj, int *expected, int desired, int success, int failure);
int cplus_atomic_compare_exchange_weak_int(int *obj, int *expected, int desired);
int cplus_atomic_compare_exchange_weak_explicit_int(int *obj, int *expected, int desired, int success, int failure);
int cplus_atomic_fetch_add_int(int *obj, int operand);
int cplus_atomic_fetch_add_explicit_int(int *obj, int operand, int order);
int cplus_atomic_fetch_sub_int(int *obj, int operand);
int cplus_atomic_fetch_sub_explicit_int(int *obj, int operand, int order);
int cplus_atomic_fetch_or_int(int *obj, int operand);
int cplus_atomic_fetch_or_explicit_int(int *obj, int operand, int order);
int cplus_atomic_fetch_xor_int(int *obj, int operand);
int cplus_atomic_fetch_xor_explicit_int(int *obj, int operand, int order);
int cplus_atomic_fetch_and_int(int *obj, int operand);
int cplus_atomic_fetch_and_explicit_int(int *obj, int operand, int order);
int cplus_atomic_fetch_nand_int(int *obj, int operand);
int cplus_atomic_fetch_nand_explicit_int(int *obj, int operand, int order);
int cplus_kill_dependency_int(int value);

/* storage type: unsigned int */
void cplus_atomic_init_uint(unsigned int *obj, unsigned int desired);
int cplus_atomic_is_lock_free_uint(const unsigned int *obj);
void cplus_atomic_store_uint(unsigned int *obj, unsigned int desired);
void cplus_atomic_store_explicit_uint(unsigned int *obj, unsigned int desired, int order);
unsigned int cplus_atomic_load_uint(const unsigned int *obj);
unsigned int cplus_atomic_load_explicit_uint(const unsigned int *obj, int order);
unsigned int cplus_atomic_exchange_uint(unsigned int *obj, unsigned int desired);
unsigned int cplus_atomic_exchange_explicit_uint(unsigned int *obj, unsigned int desired, int order);
int cplus_atomic_compare_exchange_strong_uint(unsigned int *obj, unsigned int *expected, unsigned int desired);
int cplus_atomic_compare_exchange_strong_explicit_uint(unsigned int *obj, unsigned int *expected, unsigned int desired, int success, int failure);
int cplus_atomic_compare_exchange_weak_uint(unsigned int *obj, unsigned int *expected, unsigned int desired);
int cplus_atomic_compare_exchange_weak_explicit_uint(unsigned int *obj, unsigned int *expected, unsigned int desired, int success, int failure);
unsigned int cplus_atomic_fetch_add_uint(unsigned int *obj, unsigned int operand);
unsigned int cplus_atomic_fetch_add_explicit_uint(unsigned int *obj, unsigned int operand, int order);
unsigned int cplus_atomic_fetch_sub_uint(unsigned int *obj, unsigned int operand);
unsigned int cplus_atomic_fetch_sub_explicit_uint(unsigned int *obj, unsigned int operand, int order);
unsigned int cplus_atomic_fetch_or_uint(unsigned int *obj, unsigned int operand);
unsigned int cplus_atomic_fetch_or_explicit_uint(unsigned int *obj, unsigned int operand, int order);
unsigned int cplus_atomic_fetch_xor_uint(unsigned int *obj, unsigned int operand);
unsigned int cplus_atomic_fetch_xor_explicit_uint(unsigned int *obj, unsigned int operand, int order);
unsigned int cplus_atomic_fetch_and_uint(unsigned int *obj, unsigned int operand);
unsigned int cplus_atomic_fetch_and_explicit_uint(unsigned int *obj, unsigned int operand, int order);
unsigned int cplus_atomic_fetch_nand_uint(unsigned int *obj, unsigned int operand);
unsigned int cplus_atomic_fetch_nand_explicit_uint(unsigned int *obj, unsigned int operand, int order);
unsigned int cplus_kill_dependency_uint(unsigned int value);

/* storage type: long */
void cplus_atomic_init_long(long *obj, long desired);
int cplus_atomic_is_lock_free_long(const long *obj);
void cplus_atomic_store_long(long *obj, long desired);
void cplus_atomic_store_explicit_long(long *obj, long desired, int order);
long cplus_atomic_load_long(const long *obj);
long cplus_atomic_load_explicit_long(const long *obj, int order);
long cplus_atomic_exchange_long(long *obj, long desired);
long cplus_atomic_exchange_explicit_long(long *obj, long desired, int order);
int cplus_atomic_compare_exchange_strong_long(long *obj, long *expected, long desired);
int cplus_atomic_compare_exchange_strong_explicit_long(long *obj, long *expected, long desired, int success, int failure);
int cplus_atomic_compare_exchange_weak_long(long *obj, long *expected, long desired);
int cplus_atomic_compare_exchange_weak_explicit_long(long *obj, long *expected, long desired, int success, int failure);
long cplus_atomic_fetch_add_long(long *obj, long operand);
long cplus_atomic_fetch_add_explicit_long(long *obj, long operand, int order);
long cplus_atomic_fetch_sub_long(long *obj, long operand);
long cplus_atomic_fetch_sub_explicit_long(long *obj, long operand, int order);
long cplus_atomic_fetch_or_long(long *obj, long operand);
long cplus_atomic_fetch_or_explicit_long(long *obj, long operand, int order);
long cplus_atomic_fetch_xor_long(long *obj, long operand);
long cplus_atomic_fetch_xor_explicit_long(long *obj, long operand, int order);
long cplus_atomic_fetch_and_long(long *obj, long operand);
long cplus_atomic_fetch_and_explicit_long(long *obj, long operand, int order);
long cplus_atomic_fetch_nand_long(long *obj, long operand);
long cplus_atomic_fetch_nand_explicit_long(long *obj, long operand, int order);
long cplus_kill_dependency_long(long value);

/* storage type: unsigned long */
void cplus_atomic_init_ulong(unsigned long *obj, unsigned long desired);
int cplus_atomic_is_lock_free_ulong(const unsigned long *obj);
void cplus_atomic_store_ulong(unsigned long *obj, unsigned long desired);
void cplus_atomic_store_explicit_ulong(unsigned long *obj, unsigned long desired, int order);
unsigned long cplus_atomic_load_ulong(const unsigned long *obj);
unsigned long cplus_atomic_load_explicit_ulong(const unsigned long *obj, int order);
unsigned long cplus_atomic_exchange_ulong(unsigned long *obj, unsigned long desired);
unsigned long cplus_atomic_exchange_explicit_ulong(unsigned long *obj, unsigned long desired, int order);
int cplus_atomic_compare_exchange_strong_ulong(unsigned long *obj, unsigned long *expected, unsigned long desired);
int cplus_atomic_compare_exchange_strong_explicit_ulong(unsigned long *obj, unsigned long *expected, unsigned long desired, int success, int failure);
int cplus_atomic_compare_exchange_weak_ulong(unsigned long *obj, unsigned long *expected, unsigned long desired);
int cplus_atomic_compare_exchange_weak_explicit_ulong(unsigned long *obj, unsigned long *expected, unsigned long desired, int success, int failure);
unsigned long cplus_atomic_fetch_add_ulong(unsigned long *obj, unsigned long operand);
unsigned long cplus_atomic_fetch_add_explicit_ulong(unsigned long *obj, unsigned long operand, int order);
unsigned long cplus_atomic_fetch_sub_ulong(unsigned long *obj, unsigned long operand);
unsigned long cplus_atomic_fetch_sub_explicit_ulong(unsigned long *obj, unsigned long operand, int order);
unsigned long cplus_atomic_fetch_or_ulong(unsigned long *obj, unsigned long operand);
unsigned long cplus_atomic_fetch_or_explicit_ulong(unsigned long *obj, unsigned long operand, int order);
unsigned long cplus_atomic_fetch_xor_ulong(unsigned long *obj, unsigned long operand);
unsigned long cplus_atomic_fetch_xor_explicit_ulong(unsigned long *obj, unsigned long operand, int order);
unsigned long cplus_atomic_fetch_and_ulong(unsigned long *obj, unsigned long operand);
unsigned long cplus_atomic_fetch_and_explicit_ulong(unsigned long *obj, unsigned long operand, int order);
unsigned long cplus_atomic_fetch_nand_ulong(unsigned long *obj, unsigned long operand);
unsigned long cplus_atomic_fetch_nand_explicit_ulong(unsigned long *obj, unsigned long operand, int order);
unsigned long cplus_kill_dependency_ulong(unsigned long value);

/* storage type: long long */
void cplus_atomic_init_llong(long long *obj, long long desired);
int cplus_atomic_is_lock_free_llong(const long long *obj);
void cplus_atomic_store_llong(long long *obj, long long desired);
void cplus_atomic_store_explicit_llong(long long *obj, long long desired, int order);
long long cplus_atomic_load_llong(const long long *obj);
long long cplus_atomic_load_explicit_llong(const long long *obj, int order);
long long cplus_atomic_exchange_llong(long long *obj, long long desired);
long long cplus_atomic_exchange_explicit_llong(long long *obj, long long desired, int order);
int cplus_atomic_compare_exchange_strong_llong(long long *obj, long long *expected, long long desired);
int cplus_atomic_compare_exchange_strong_explicit_llong(long long *obj, long long *expected, long long desired, int success, int failure);
int cplus_atomic_compare_exchange_weak_llong(long long *obj, long long *expected, long long desired);
int cplus_atomic_compare_exchange_weak_explicit_llong(long long *obj, long long *expected, long long desired, int success, int failure);
long long cplus_atomic_fetch_add_llong(long long *obj, long long operand);
long long cplus_atomic_fetch_add_explicit_llong(long long *obj, long long operand, int order);
long long cplus_atomic_fetch_sub_llong(long long *obj, long long operand);
long long cplus_atomic_fetch_sub_explicit_llong(long long *obj, long long operand, int order);
long long cplus_atomic_fetch_or_llong(long long *obj, long long operand);
long long cplus_atomic_fetch_or_explicit_llong(long long *obj, long long operand, int order);
long long cplus_atomic_fetch_xor_llong(long long *obj, long long operand);
long long cplus_atomic_fetch_xor_explicit_llong(long long *obj, long long operand, int order);
long long cplus_atomic_fetch_and_llong(long long *obj, long long operand);
long long cplus_atomic_fetch_and_explicit_llong(long long *obj, long long operand, int order);
long long cplus_atomic_fetch_nand_llong(long long *obj, long long operand);
long long cplus_atomic_fetch_nand_explicit_llong(long long *obj, long long operand, int order);
long long cplus_kill_dependency_llong(long long value);

/* storage type: unsigned long long */
void cplus_atomic_init_ullong(unsigned long long *obj, unsigned long long desired);
int cplus_atomic_is_lock_free_ullong(const unsigned long long *obj);
void cplus_atomic_store_ullong(unsigned long long *obj, unsigned long long desired);
void cplus_atomic_store_explicit_ullong(unsigned long long *obj, unsigned long long desired, int order);
unsigned long long cplus_atomic_load_ullong(const unsigned long long *obj);
unsigned long long cplus_atomic_load_explicit_ullong(const unsigned long long *obj, int order);
unsigned long long cplus_atomic_exchange_ullong(unsigned long long *obj, unsigned long long desired);
unsigned long long cplus_atomic_exchange_explicit_ullong(unsigned long long *obj, unsigned long long desired, int order);
int cplus_atomic_compare_exchange_strong_ullong(unsigned long long *obj, unsigned long long *expected, unsigned long long desired);
int cplus_atomic_compare_exchange_strong_explicit_ullong(unsigned long long *obj, unsigned long long *expected, unsigned long long desired, int success, int failure);
int cplus_atomic_compare_exchange_weak_ullong(unsigned long long *obj, unsigned long long *expected, unsigned long long desired);
int cplus_atomic_compare_exchange_weak_explicit_ullong(unsigned long long *obj, unsigned long long *expected, unsigned long long desired, int success, int failure);
unsigned long long cplus_atomic_fetch_add_ullong(unsigned long long *obj, unsigned long long operand);
unsigned long long cplus_atomic_fetch_add_explicit_ullong(unsigned long long *obj, unsigned long long operand, int order);
unsigned long long cplus_atomic_fetch_sub_ullong(unsigned long long *obj, unsigned long long operand);
unsigned long long cplus_atomic_fetch_sub_explicit_ullong(unsigned long long *obj, unsigned long long operand, int order);
unsigned long long cplus_atomic_fetch_or_ullong(unsigned long long *obj, unsigned long long operand);
unsigned long long cplus_atomic_fetch_or_explicit_ullong(unsigned long long *obj, unsigned long long operand, int order);
unsigned long long cplus_atomic_fetch_xor_ullong(unsigned long long *obj, unsigned long long operand);
unsigned long long cplus_atomic_fetch_xor_explicit_ullong(unsigned long long *obj, unsigned long long operand, int order);
unsigned long long cplus_atomic_fetch_and_ullong(unsigned long long *obj, unsigned long long operand);
unsigned long long cplus_atomic_fetch_and_explicit_ullong(unsigned long long *obj, unsigned long long operand, int order);
unsigned long long cplus_atomic_fetch_nand_ullong(unsigned long long *obj, unsigned long long operand);
unsigned long long cplus_atomic_fetch_nand_explicit_ullong(unsigned long long *obj, unsigned long long operand, int order);
unsigned long long cplus_kill_dependency_ullong(unsigned long long value);

/* storage type: void * (load/store/exchange/CAS/init/lock-free only;
 * arithmetic fetch_* is not defined for void* and remains BLOCKED). */
void cplus_atomic_init_ptr(void **obj, void * desired);
int cplus_atomic_is_lock_free_ptr(void *const *obj);
void cplus_atomic_store_ptr(void **obj, void * desired);
void cplus_atomic_store_explicit_ptr(void **obj, void * desired, int order);
void * cplus_atomic_load_ptr(void *const *obj);
void * cplus_atomic_load_explicit_ptr(void *const *obj, int order);
void * cplus_atomic_exchange_ptr(void **obj, void * desired);
void * cplus_atomic_exchange_explicit_ptr(void **obj, void * desired, int order);
int cplus_atomic_compare_exchange_strong_ptr(void **obj, void * *expected, void * desired);
int cplus_atomic_compare_exchange_strong_explicit_ptr(void **obj, void * *expected, void * desired, int success, int failure);
int cplus_atomic_compare_exchange_weak_ptr(void **obj, void * *expected, void * desired);
int cplus_atomic_compare_exchange_weak_explicit_ptr(void **obj, void * *expected, void * desired, int success, int failure);
void * cplus_kill_dependency_ptr(void * value);

/* ---- atomic_flag operations (type is fixed, no suffix) ----------------- */
int  cplus_atomic_flag_test_and_set(cplus_atomic_flag *obj);
int  cplus_atomic_flag_test_and_set_explicit(cplus_atomic_flag *obj, int order);
void cplus_atomic_flag_clear(cplus_atomic_flag *obj);
void cplus_atomic_flag_clear_explicit(cplus_atomic_flag *obj, int order);

/* ---- Fences ------------------------------------------------------------- */
void cplus_atomic_thread_fence(int order);
void cplus_atomic_signal_fence(int order);

#ifdef __cplusplus
}
#endif
#endif /* CPLUS_STDATOMIC_H */
