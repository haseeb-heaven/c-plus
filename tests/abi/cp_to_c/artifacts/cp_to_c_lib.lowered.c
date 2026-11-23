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
        return self->base + x;
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
        if (r->i != self->want_i)   return CP_TO_C_ERR_RECORD_I;
        if (r->u != self->want_u)   return CP_TO_C_ERR_RECORD_U;
        if (r->d != 3.5)            return CP_TO_C_ERR_RECORD_D;
        if (r->p == (void *)0)      return CP_TO_C_ERR_RECORD_P;
        if (r->n != 7u)             return CP_TO_C_ERR_RECORD_N;
        if (r->q != 0x0102030405060708LL) return CP_TO_C_ERR_RECORD_Q;
        if (__builtin_memcmp(r->tag, cp_to_c_expected_tag, 3u) != 0)
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
        for (i = 0u; i < n; ++i) {
            double elem = 0.0;
            __builtin_memcpy(&elem, p + i, sizeof(elem));
            acc += elem;
        }
        return acc * self->factor;
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
        if (idx >= self->count) return CP_TO_C_ERR_RANGE;
        cell = value;
        __builtin_memcpy(self->data + idx, &cell, sizeof(cell));
        return CP_TO_C_OK;
    }
#line 73 "tests/abi/cp_to_c/cp_to_c_lib.cp"
int CpBuffer_read(CpBuffer *self, size_t idx , int32_t * out)
#line 73 "tests/abi/cp_to_c/cp_to_c_lib.cp"
{
        int32_t cell = 0;
        if (out == (int32_t *)0) return CP_TO_C_ERR_NULL;
        if (idx >= self->count) return CP_TO_C_ERR_RANGE;
        __builtin_memcpy(&cell, self->data + idx, sizeof(cell));
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
    if (cplus_alloc_get_meta(h, out) != 0 || !out->is_live)
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
    for (i = 0u; i < elem_count; ++i) {
        __builtin_memcpy((int32_t *)raw + i, &seed, sizeof(seed));
        seed = seed + 1;
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
    if (*h_inout == (void *)0) return CP_TO_C_ERR_NULL;
    if (cplus_free_checked(h_inout) != 0) return CP_TO_C_ERR_FREE;
    return CP_TO_C_OK;
}

int cp_to_c_echo_err(int code)
{
    return code;
}