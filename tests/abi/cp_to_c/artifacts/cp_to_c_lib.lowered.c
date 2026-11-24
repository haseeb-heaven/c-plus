extern void cplus_check_nonnull(const void *, const char *, int, const char *);
#if defined(__GNUC__) || defined(__clang__)
#define CPLUS_NONNULL(P) ({ __typeof__(P) __cplus_nn_p = (P); \
  cplus_check_nonnull((const void *)__cplus_nn_p, __FILE__, __LINE__, #P); \
  __cplus_nn_p; })
#else
#define CPLUS_NONNULL(P) \
  (cplus_check_nonnull((const void *)(P), __FILE__, __LINE__, #P), (P))
#endif
#include <stdint.h>
#if defined(__GNUC__) || defined(__clang__)
#  define CPLUS_STATIC_INLINE __attribute__((unused)) static
#else
#  define CPLUS_STATIC_INLINE static
#endif
extern int64_t __cplus_checked_add_i64(int64_t, int64_t, unsigned, unsigned);
extern int64_t __cplus_checked_sub_i64(int64_t, int64_t, unsigned, unsigned);
extern int64_t __cplus_checked_mul_i64(int64_t, int64_t, unsigned, unsigned);
extern int64_t __cplus_checked_div_i64(int64_t, int64_t, unsigned, unsigned);
extern int64_t __cplus_checked_rem_i64(int64_t, int64_t, unsigned, unsigned);
extern int64_t __cplus_checked_shl_i64(int64_t, int64_t, unsigned, unsigned);
extern int64_t __cplus_checked_shr_i64(int64_t, int64_t, unsigned, unsigned);
extern int64_t __cplus_checked_narrow_i64(uint64_t, unsigned, unsigned, unsigned, unsigned);
#if defined(__has_builtin)
#  if __has_builtin(__builtin_add_overflow) && __has_builtin(__builtin_sub_overflow) && __has_builtin(__builtin_mul_overflow)
#    define CPLUS_HAVE_INLINE_ARITH 1
#  endif
#endif
#ifdef CPLUS_HAVE_INLINE_ARITH
#define CPLUS_ARITH_INLINE CPLUS_STATIC_INLINE
CPLUS_ARITH_INLINE int64_t __cplus_inline_add_i64(int64_t a, int64_t b, unsigned bits, unsigned sgn) {
  int64_t r; int32_t r32;
  if (bits == 32) {
    if (__builtin_add_overflow((int32_t)a, (int32_t)b, &r32))
      return __cplus_checked_add_i64(a, b, bits, sgn);
    return (int64_t)r32;
  }
  if (bits == 64 && sgn) {
    if (__builtin_add_overflow(a, b, &r))
      return __cplus_checked_add_i64(a, b, bits, sgn);
    return r;
  }
  return __cplus_checked_add_i64(a, b, bits, sgn);
}
CPLUS_ARITH_INLINE int64_t __cplus_inline_sub_i64(int64_t a, int64_t b, unsigned bits, unsigned sgn) {
  int64_t r; int32_t r32;
  if (bits == 32) {
    if (__builtin_sub_overflow((int32_t)a, (int32_t)b, &r32))
      return __cplus_checked_sub_i64(a, b, bits, sgn);
    return (int64_t)r32;
  }
  if (bits == 64 && sgn) {
    if (__builtin_sub_overflow(a, b, &r))
      return __cplus_checked_sub_i64(a, b, bits, sgn);
    return r;
  }
  return __cplus_checked_sub_i64(a, b, bits, sgn);
}
CPLUS_ARITH_INLINE int64_t __cplus_inline_mul_i64(int64_t a, int64_t b, unsigned bits, unsigned sgn) {
  int64_t r; int32_t r32;
  if (bits == 32) {
    if (__builtin_mul_overflow((int32_t)a, (int32_t)b, &r32))
      return __cplus_checked_mul_i64(a, b, bits, sgn);
    return (int64_t)r32;
  }
  if (bits == 64 && sgn) {
    if (__builtin_mul_overflow(a, b, &r))
      return __cplus_checked_mul_i64(a, b, bits, sgn);
    return r;
  }
  return __cplus_checked_mul_i64(a, b, bits, sgn);
}
CPLUS_ARITH_INLINE int64_t __cplus_inline_div_i64(int64_t a, int64_t b, unsigned bits, unsigned sgn) {
  if (b == 0)
    return __cplus_checked_div_i64(a, b, bits, sgn);
  if (sgn && bits > 1 && bits <= 64 && b == -1 &&
      a == (bits == 32 ? (int64_t)INT32_MIN : INT64_MIN))
    return __cplus_checked_div_i64(a, b, bits, sgn);
  return a / b;
}
CPLUS_ARITH_INLINE int64_t __cplus_inline_rem_i64(int64_t a, int64_t b, unsigned bits, unsigned sgn) {
  if (b == 0)
    return __cplus_checked_rem_i64(a, b, bits, sgn);
  if (sgn && bits > 1 && bits <= 64 && b == -1 &&
      a == (bits == 32 ? (int64_t)INT32_MIN : INT64_MIN))
    return __cplus_checked_rem_i64(a, b, bits, sgn);
  return a % b;
}
CPLUS_ARITH_INLINE int64_t __cplus_inline_shl_i64(int64_t a, int64_t b, unsigned bits, unsigned sgn) {
  if (b < 0 || (unsigned long long)b >= bits)
    return __cplus_checked_shl_i64(a, b, bits, sgn);
  if (sgn) {
    uint64_t ua = (uint64_t)a; unsigned c = (unsigned)b;
    uint64_t limit = (~(uint64_t)0) >> (64 - bits);
    if (a < 0 || ((ua << c) & ~limit))
      return __cplus_checked_shl_i64(a, b, bits, sgn);
  }
  return (int64_t)((uint64_t)a << (uint64_t)b);
}
CPLUS_ARITH_INLINE int64_t __cplus_inline_shr_i64(int64_t a, int64_t b, unsigned bits, unsigned sgn) {
  if (b < 0 || (unsigned long long)b >= bits)
    return __cplus_checked_shr_i64(a, b, bits, sgn);
  return (int64_t)(a >> (uint64_t)b);
}
CPLUS_ARITH_INLINE int64_t __cplus_inline_narrow_i64(
    uint64_t v, unsigned sb, unsigned ss, unsigned db, unsigned ds) {
  uint64_t sv = v;
  if (ss && sb > 0 && sb < 64 && ((v >> (sb - 1)) & 1u))
    sv = v | (~((uint64_t)0) << sb);
  if (db < 64) {
    uint64_t m = (((uint64_t)1) << db) - 1;
    uint64_t t = sv & m;
    if (ds && ((t >> (db - 1)) & 1u)) t |= ~m;
    if (t == sv) return (int64_t)t;
  } else if (sv == v) {
    return (int64_t)v;
  }
  return __cplus_checked_narrow_i64(v, sb, ss, db, ds);
}
#else
#define __cplus_inline_add_i64 __cplus_checked_add_i64
#define __cplus_inline_sub_i64 __cplus_checked_sub_i64
#define __cplus_inline_mul_i64 __cplus_checked_mul_i64
#define __cplus_inline_div_i64 __cplus_checked_div_i64
#define __cplus_inline_rem_i64 __cplus_checked_rem_i64
#define __cplus_inline_shl_i64 __cplus_checked_shl_i64
#define __cplus_inline_shr_i64 __cplus_checked_shr_i64
#define __cplus_inline_narrow_i64 __cplus_checked_narrow_i64
#endif
#if defined(__GNUC__) || defined(__clang__)
#define CPLUS_CMPD_BIN(OP, P, T, B, BT, SG) \
  ({ __typeof__(&(P)) __cplus_ap = &(P); \
     *__cplus_ap = (T)__cplus_inline_##OP##_i64( \
         (int64_t)(*__cplus_ap), (int64_t)(B), (BT), (SG)); \
     *__cplus_ap; })
#define CPLUS_INC(OP, P, T, BT, SG, D, POST) \
  ({ __typeof__(&(P)) __cplus_ap = &(P); \
     __typeof__(*__cplus_ap) __cplus_ov = *__cplus_ap; \
     *__cplus_ap = (T)__cplus_inline_##OP##_i64( \
         (int64_t)__cplus_ov, (int64_t)(D), (BT), (SG)); \
     (POST) ? __cplus_ov : *__cplus_ap; })
#else
#define CPLUS_CMPD_BIN(OP, P, T, B, BT, SG) \
  ((P) = (T)__cplus_inline_##OP##_i64( \
      (int64_t)(P), (int64_t)(B), (BT), (SG)))
#define CPLUS_INC(OP, P, T, BT, SG, D, POST) \
  ((P) = (T)__cplus_inline_##OP##_i64((int64_t)(P), \
      (int64_t)(D), (BT), (SG)))
#endif
#define CPLUS_NEG(X, T, BT, SG) \
  ((T)__cplus_inline_sub_i64(0, (int64_t)(X), (BT), (SG)))
#line 1 "tests/abi/cp_to_c/cp_to_c_lib.cp"
/* C+ implementation lowered by ./build/c+ (STRICT mode) to plain C symbols,
 * archived with /usr/bin/ar, then called from an ordinary C translation unit
 * compiled with /usr/bin/clang.
 *
 * Every `class` below is a genuine C+ class: the lowering emits a plain C
 * struct plus an ordinary function named <Class>_<method> with an explicit
 * `self` parameter.  No Itanium/C++ name mangling is involved.  Each exported
 * entry point keeps the plain-C signature declared in cp_to_c_shared.h.
 *
 * STRICT-mode rules honoured throughout this file:
 *   - class-typed locals have globally unique names (lowering keys method
 *     dispatch by variable name across the translation unit);
 *   - no method call on a returned temporary -> named temporaries only;
 *   - no variable division or modulo (checked divide-by-zero is rejected);
 *   - element access through a pointer parameter uses __builtin_memcpy into a
 *     local instead of p[i] (unverifiable-extent subscript is rejected).
 */
#include "cp_to_c_shared.h"
#include "cplus_libc.h"

/* Expected byte pattern for cp_to_c_record.tag (0xAA, 0xBB, 0xCC). */
static const unsigned char cp_to_c_expected_tag[3] = {0xAAu, 0xBBu, 0xCCu};

/* ---- C+ classes: each method lowers to a plain <Class>_<method> symbol --- */

struct CpAdder {
int32_t base;
};
typedef struct CpAdder CpAdder;
#line 28 "tests/abi/cp_to_c/cp_to_c_lib.cp"
int32_t CpAdder_apply(CpAdder *self, int32_t x)
#line 28 "tests/abi/cp_to_c/cp_to_c_lib.cp"
{
        return ((int)__cplus_inline_add_i64((int64_t)(CPLUS_NONNULL(self)->base ), (int64_t)( x), 32, 1));
    }
#line 33 "tests/abi/cp_to_c/cp_to_c_lib.cp"


struct CpRecordValidator {
int32_t  want_i;
uint32_t want_u;
};
typedef struct CpRecordValidator CpRecordValidator;
#line 36 "tests/abi/cp_to_c/cp_to_c_lib.cp"
int CpRecordValidator_check(CpRecordValidator *self, const cp_to_c_record * r)
#line 36 "tests/abi/cp_to_c/cp_to_c_lib.cp"
{
        if (CPLUS_NONNULL(r)->i != CPLUS_NONNULL(self)->want_i)   return CP_TO_C_ERR_RECORD_I;
        if (CPLUS_NONNULL(r)->u != CPLUS_NONNULL(self)->want_u)   return CP_TO_C_ERR_RECORD_U;
        if (CPLUS_NONNULL(r)->d != 3.5)            return CP_TO_C_ERR_RECORD_D;
        if (CPLUS_NONNULL(r)->p == (void *)0)      return CP_TO_C_ERR_RECORD_P;
        if (CPLUS_NONNULL(r)->n != 7u)             return CP_TO_C_ERR_RECORD_N;
        if (CPLUS_NONNULL(r)->q != 0x0102030405060708LL) return CP_TO_C_ERR_RECORD_Q;
        if (__builtin_memcmp(CPLUS_NONNULL(r)->tag, cp_to_c_expected_tag, 3u) != 0)
            return CP_TO_C_ERR_RECORD_TAG;
        return CP_TO_C_OK;
    }
#line 49 "tests/abi/cp_to_c/cp_to_c_lib.cp"


struct CpScaler {
double factor;
};
typedef struct CpScaler CpScaler;
#line 51 "tests/abi/cp_to_c/cp_to_c_lib.cp"
double CpScaler_apply(CpScaler *self, const double * p , size_t n)
#line 51 "tests/abi/cp_to_c/cp_to_c_lib.cp"
{
        double acc = 0.0;
        size_t i;
        for (i = 0u; i < n; CPLUS_INC(add, i, unsigned long, 64, 0, 1, 0)) {
            double elem = 0.0;
            __builtin_memcpy(&elem, p + i, sizeof(elem));
            acc += elem;
        }
        return acc * CPLUS_NONNULL(self)->factor;
    }
#line 63 "tests/abi/cp_to_c/cp_to_c_lib.cp"


struct CpBuffer {
int32_t *data;
size_t   count;
};
typedef struct CpBuffer CpBuffer;
#line 66 "tests/abi/cp_to_c/cp_to_c_lib.cp"
int CpBuffer_write(CpBuffer *self, size_t idx , int32_t value)
#line 66 "tests/abi/cp_to_c/cp_to_c_lib.cp"
{
        int32_t cell = 0;
        if (idx >= CPLUS_NONNULL(self)->count) return CP_TO_C_ERR_RANGE;
        cell = value;
        __builtin_memcpy(CPLUS_NONNULL(self)->data + idx, &cell, sizeof(cell));
        return CP_TO_C_OK;
    }
#line 73 "tests/abi/cp_to_c/cp_to_c_lib.cp"
int CpBuffer_read(CpBuffer *self, size_t idx , int32_t * out)
#line 73 "tests/abi/cp_to_c/cp_to_c_lib.cp"
{
        int32_t cell = 0;
        if (out == (int32_t *)0) return CP_TO_C_ERR_NULL;
        if (idx >= CPLUS_NONNULL(self)->count) return CP_TO_C_ERR_RANGE;
        __builtin_memcpy(&cell, CPLUS_NONNULL(self)->data + idx, sizeof(cell));
        __builtin_memcpy(out, &cell, sizeof(cell));
        return CP_TO_C_OK;
    }
#line 83 "tests/abi/cp_to_c/cp_to_c_lib.cp"


struct CpLayoutProbe {
    char _cplus_empty;
};
typedef struct CpLayoutProbe CpLayoutProbe;
#line 84 "tests/abi/cp_to_c/cp_to_c_lib.cp"
int CpLayoutProbe_fill(CpLayoutProbe *self, cp_to_c_layout * out)
#line 84 "tests/abi/cp_to_c/cp_to_c_lib.cp"
{
        cp_to_c_layout local;
        if (out == (cp_to_c_layout *)0) return CP_TO_C_ERR_NULL;
        local.record_size  = sizeof(cp_to_c_record);
        local.record_align = __alignof__(cp_to_c_record);
        local.off_i        = offsetof(cp_to_c_record, i);
        local.off_u        = offsetof(cp_to_c_record, u);
        local.off_d        = offsetof(cp_to_c_record, d);
        local.off_p        = offsetof(cp_to_c_record, p);
        local.off_n        = offsetof(cp_to_c_record, n);
        local.off_q        = offsetof(cp_to_c_record, q);
        local.off_tag      = offsetof(cp_to_c_record, tag);
        local.pointer_size = sizeof(void *);
        __builtin_memcpy(out, &local, sizeof(local));
        return CP_TO_C_OK;
    }
#line 104 "tests/abi/cp_to_c/cp_to_c_lib.cp"


/* ---- shared helper: validate an opaque tracked buffer handle ------------ */

static int cp_to_c_handle_meta(const void *h, cplus_alloc_meta *out)
{
    if (h == (void *)0) return CP_TO_C_ERR_NULL;
    if (out == (cplus_alloc_meta *)0) return CP_TO_C_ERR_NULL;
    if (cplus_alloc_get_meta(h, out) != 0 || !CPLUS_NONNULL(out)->is_live)
        return CP_TO_C_ERR_META;
    return CP_TO_C_OK;
}

/* ---- exported plain-C entry points -------------------------------------- */

int cp_to_c_add(int32_t a, int32_t b)
{
    CpAdder add_eng = {a};
    return CpAdder_apply(&add_eng, b);
}

int cp_to_c_mixcall(int32_t a, double b, const void *p,
                    int32_t c, double d, size_t e, int32_t f)
{
    int32_t cell = 0;
    if (p == (const void *)0) return CP_TO_C_ERR_ARG_P;
    __builtin_memcpy(&cell, p, sizeof(cell));
    if (cell != (int32_t)0x11223344) return CP_TO_C_ERR_ARG_P;
    if (a != 11)                 return CP_TO_C_ERR_ARG_A;
    if (b != 2.5)                return CP_TO_C_ERR_ARG_B;
    if (c != 33)                 return CP_TO_C_ERR_ARG_C;
    if (d != 4.5)                return CP_TO_C_ERR_ARG_D;
    if (e != (size_t)55)         return CP_TO_C_ERR_ARG_E;
    if (f != 66)                 return CP_TO_C_ERR_ARG_F;
    return CP_TO_C_OK;
}

int cp_to_c_record_by_value(cp_to_c_record r)
{
    cp_to_c_record rec_copy = r;
    CpRecordValidator val_eng = {-12345, 0xabcdefu};
    return CpRecordValidator_check(&val_eng, &rec_copy);
}

int cp_to_c_record_by_ptr(const cp_to_c_record *r)
{
    CpRecordValidator val_eng_ptr = {-12345, 0xabcdefu};
    if (r == (const cp_to_c_record *)0) return CP_TO_C_ERR_NULL;
    return CpRecordValidator_check(&val_eng_ptr, r);
}

int cp_to_c_ptr_roundtrip(const void *p, void *out, size_t out_size)
{
    if (out == (void *)0) return CP_TO_C_ERR_NULL;
    if (out_size < sizeof(p)) return CP_TO_C_ERR_SIZE;
    __builtin_memcpy(out, &p, sizeof(p));
    return CP_TO_C_OK;
}

int cp_to_c_layout_report(cp_to_c_layout *out)
{
    CpLayoutProbe layout_eng;
    return CpLayoutProbe_fill(&layout_eng, out);
}

int cp_to_c_scale_sum(const double *p, size_t n, double factor, double *out)
{
    CpScaler scale_eng;
    double got = 0.0;
    if (p == (const double *)0 || out == (double *)0) return CP_TO_C_ERR_NULL;
    if (n == 0u || n > 16u) return CP_TO_C_ERR_SIZE;
    scale_eng.factor = factor;
    got = CpScaler_apply(&scale_eng, p, n);
    __builtin_memcpy(out, &got, sizeof(got));
    return CP_TO_C_OK;
}

void *cp_to_c_buffer_create(size_t elem_count)
{
    void *raw;
    void *handle;
    size_t i;
    int32_t seed = 7;

    if (elem_count == 0u || elem_count > 16u) return (void *)0;
    /* cplus_calloc records elem_size=sizeof(int32_t) and elem_count=elem_count,
     * so the C side can query element granularity, not just raw byte count. */
    raw = cplus_calloc(elem_count, sizeof(int32_t));
    if (raw == (void *)0) return (void *)0;
    for (i = 0u; i < elem_count; CPLUS_INC(add, i, unsigned long, 64, 0, 1, 0)) {
        __builtin_memcpy((int32_t *)raw + i, &seed, sizeof(seed));
        seed = ((int)__cplus_inline_add_i64((int64_t)(seed ), (int64_t)( 1), 32, 1));
    }
    handle = (void *)0;
    cplus_transfer_ownership(&handle, &raw);
    return handle;
}

int cp_to_c_buffer_write(void *h, size_t idx, int32_t value)
{
    cplus_alloc_meta write_meta;
    CpBuffer buf_eng_write;
    int rc;

    rc = cp_to_c_handle_meta(h, &write_meta);
    if (rc != CP_TO_C_OK) return rc;
    buf_eng_write.data = (int32_t *)h;
    buf_eng_write.count = write_meta.elem_count;
    return CpBuffer_write(&buf_eng_write, idx, value);
}

int cp_to_c_buffer_read(const void *h, size_t idx, int32_t *out)
{
    cplus_alloc_meta read_meta;
    CpBuffer buf_eng_read;
    int rc;

    rc = cp_to_c_handle_meta(h, &read_meta);
    if (rc != CP_TO_C_OK) return rc;
    buf_eng_read.data = (int32_t *)h;
    buf_eng_read.count = read_meta.elem_count;
    return CpBuffer_read(&buf_eng_read, idx, out);
}

int cp_to_c_buffer_meta(const void *h, size_t *out_byte_size,
                        size_t *out_elem_count)
{
    cplus_alloc_meta meta_meta;
    int rc;

    if (out_byte_size == (size_t *)0 || out_elem_count == (size_t *)0)
        return CP_TO_C_ERR_NULL;
    rc = cp_to_c_handle_meta(h, &meta_meta);
    if (rc != CP_TO_C_OK) return rc;
    __builtin_memcpy(out_byte_size, &meta_meta.byte_size,
                     sizeof(meta_meta.byte_size));
    __builtin_memcpy(out_elem_count, &meta_meta.elem_count,
                     sizeof(meta_meta.elem_count));
    return CP_TO_C_OK;
}

int cp_to_c_buffer_destroy(void **h_inout)
{
    if (h_inout == (void **)0) return CP_TO_C_ERR_NULL;
    if (*CPLUS_NONNULL(h_inout) == (void *)0) return CP_TO_C_ERR_NULL;
    if (cplus_free_checked(h_inout) != 0) return CP_TO_C_ERR_FREE;
    return CP_TO_C_OK;
}

int cp_to_c_echo_err(int code)
{
    return code;
}
