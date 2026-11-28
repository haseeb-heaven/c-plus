/* Ordinary C driver: compiled by /usr/bin/clang with -std=c99 -Wall -Wextra
 * -pedantic.  Every function called below is defined in a real C+ source
 * (cp_to_c_lib.cp), lowered by ./build/c+ and archived with /usr/bin/ar.
 *
 * Link line (see run_cp_to_c.sh):
 *   /usr/bin/clang ... cp_to_c_driver.c \
 *     artifacts/libcp_to_c.a build/libc/libcplus_hardened.a -lm -pthread -o ...
 *
 * This translation unit contains no C+ keywords, no C++ constructs, no
 * compiler-specific attributes and no mangled names: it is plain ISO C.
 */
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "cp_to_c_shared.h"

static int g_checks;
static int g_failed;

static void ck(const char *name, int ok)
{
    ++g_checks;
    if (!ok)
        ++g_failed;
    printf("%-56s %s\n", name, ok ? "OK" : "MISMATCH");
}

static void make_record(cp_to_c_record *r, void *self_addr)
{
    memset(r, 0, sizeof(*r));
    r->i = -12345;
    r->u = 0xabcdefu;
    r->d = 3.5;
    r->p = self_addr;
    r->n = 7u;
    r->q = 0x0102030405060708LL;
    r->tag[0] = 0xAAu;
    r->tag[1] = 0xBBu;
    r->tag[2] = 0xCCu;
}

int main(void)
{
    cp_to_c_record r;
    cp_to_c_record bad;
    unsigned char before[sizeof r];
    unsigned char after[sizeof r];
    cp_to_c_layout lay;
    void *slot;
    void *buf;
    size_t bsz;
    size_t ecnt;
    double vals[4];
    double scaled;
    int32_t cell;
    int32_t got;
    size_t idx;
    int rc;
    int ecode;

    printf("cp_to_c_driver: sizeof(void*)=%llu sizeof(record)=%llu "
           "align(record)=%llu\n",
           (unsigned long long)sizeof(void *),
           (unsigned long long)sizeof(cp_to_c_record),
           (unsigned long long)__alignof__(cp_to_c_record));

    /* ---- pointer representation (8 bytes on LP64) ----------------------- */
    ck("PTR C side sizeof(void *) == 8", sizeof(void *) == 8u);
    memset(&lay, 0, sizeof lay);
    ck("PTR C+ layout_report rc == OK",
       cp_to_c_layout_report(&lay) == CP_TO_C_OK);
    ck("PTR C+ pointer_size == 8", lay.pointer_size == 8u);
    ck("PTR C+ pointer_size == C pointer_size",
       lay.pointer_size == sizeof(void *));

    make_record(&r, (void *)&r);
    slot = (void *)0;
    rc = cp_to_c_ptr_roundtrip((const void *)&r, &slot, sizeof slot);
    ck("PTR roundtrip rc == OK", rc == CP_TO_C_OK);
    ck("PTR roundtrip is bit-identical", slot == (void *)&r);
    rc = cp_to_c_ptr_roundtrip((const void *)&r, &slot, 4u);
    ck("PTR roundtrip short out -> ERR_SIZE", rc == CP_TO_C_ERR_SIZE);
    rc = cp_to_c_ptr_roundtrip((const void *)&r, (void *)0, sizeof slot);
    ck("PTR roundtrip null out -> ERR_NULL", rc == CP_TO_C_ERR_NULL);

    /* ---- struct layout agreement ---------------------------------------- */
    ck("LAYOUT record_size matches C", lay.record_size == sizeof(cp_to_c_record));
    ck("LAYOUT record_align matches C",
       lay.record_align == __alignof__(cp_to_c_record));
    ck("LAYOUT off_i matches C", lay.off_i == offsetof(cp_to_c_record, i));
    ck("LAYOUT off_u matches C", lay.off_u == offsetof(cp_to_c_record, u));
    ck("LAYOUT off_d matches C", lay.off_d == offsetof(cp_to_c_record, d));
    ck("LAYOUT off_p matches C", lay.off_p == offsetof(cp_to_c_record, p));
    ck("LAYOUT off_n matches C", lay.off_n == offsetof(cp_to_c_record, n));
    ck("LAYOUT off_q matches C", lay.off_q == offsetof(cp_to_c_record, q));
    ck("LAYOUT off_tag matches C", lay.off_tag == offsetof(cp_to_c_record, tag));
    printf("cp_to_c_driver: C+ layout size=%llu align=%llu "
           "i=%llu u=%llu d=%llu p=%llu n=%llu q=%llu tag=%llu\n",
           (unsigned long long)lay.record_size,
           (unsigned long long)lay.record_align,
           (unsigned long long)lay.off_i, (unsigned long long)lay.off_u,
           (unsigned long long)lay.off_d, (unsigned long long)lay.off_p,
           (unsigned long long)lay.off_n, (unsigned long long)lay.off_q,
           (unsigned long long)lay.off_tag);

    /* ---- calling convention / signatures -------------------------------- */
    ck("CALL C->C+ add(20,22) == 42", cp_to_c_add(20, 22) == 42);
    ck("CALL C->C+ add(-5,5) == 0", cp_to_c_add(-5, 5) == 0);
    ck("CALL C->C+ add(-2147483648,1) == -2147483647",
       cp_to_c_add(INT32_MIN, 1) == -2147483647);

    cell = (int32_t)0x11223344;
    ck("CALL mixcall 7 mixed args accepted",
       cp_to_c_mixcall(11, 2.5, &cell, 33, 4.5, (size_t)55, 66) == CP_TO_C_OK);
    ck("ERR mixcall arg a detected",
       cp_to_c_mixcall(99, 2.5, &cell, 33, 4.5, (size_t)55, 66) == CP_TO_C_ERR_ARG_A);
    ck("ERR mixcall arg b detected",
       cp_to_c_mixcall(11, 9.5, &cell, 33, 4.5, (size_t)55, 66) == CP_TO_C_ERR_ARG_B);
    ck("ERR mixcall arg p detected",
       cp_to_c_mixcall(11, 2.5, (const void *)0, 33, 4.5, (size_t)55, 66) == CP_TO_C_ERR_ARG_P);
    cell = 1;
    ck("ERR mixcall pointed-to value detected",
       cp_to_c_mixcall(11, 2.5, &cell, 33, 4.5, (size_t)55, 66) == CP_TO_C_ERR_ARG_P);
    cell = (int32_t)0x11223344;
    ck("ERR mixcall arg c detected",
       cp_to_c_mixcall(11, 2.5, &cell, 99, 4.5, (size_t)55, 66) == CP_TO_C_ERR_ARG_C);
    ck("ERR mixcall arg d detected",
       cp_to_c_mixcall(11, 2.5, &cell, 33, 9.5, (size_t)55, 66) == CP_TO_C_ERR_ARG_D);
    ck("ERR mixcall arg e detected",
       cp_to_c_mixcall(11, 2.5, &cell, 33, 4.5, (size_t)99, 66) == CP_TO_C_ERR_ARG_E);
    ck("ERR mixcall arg f detected",
       cp_to_c_mixcall(11, 2.5, &cell, 33, 4.5, (size_t)55, 99) == CP_TO_C_ERR_ARG_F);

    /* ---- struct by value / by pointer ----------------------------------- */
    memcpy(before, &r, sizeof before);
    ck("STRUCT by value accepted", cp_to_c_record_by_value(r) == CP_TO_C_OK);
    memcpy(after, &r, sizeof after);
    ck("STRUCT by value leaves caller bytes unchanged",
       memcmp(before, after, sizeof before) == 0);
    ck("STRUCT by pointer accepted", cp_to_c_record_by_ptr(&r) == CP_TO_C_OK);
    ck("STRUCT null pointer -> ERR_NULL",
       cp_to_c_record_by_ptr((const cp_to_c_record *)0) == CP_TO_C_ERR_NULL);

    bad = r; bad.i = 1;
    ck("ERR field i by value", cp_to_c_record_by_value(bad) == CP_TO_C_ERR_RECORD_I);
    ck("ERR field i by pointer", cp_to_c_record_by_ptr(&bad) == CP_TO_C_ERR_RECORD_I);
    bad = r; bad.u = 1u;
    ck("ERR field u", cp_to_c_record_by_value(bad) == CP_TO_C_ERR_RECORD_U);
    bad = r; bad.d = 3.6;
    ck("ERR field d", cp_to_c_record_by_value(bad) == CP_TO_C_ERR_RECORD_D);
    bad = r; bad.p = (void *)0;
    ck("ERR field p", cp_to_c_record_by_value(bad) == CP_TO_C_ERR_RECORD_P);
    bad = r; bad.n = 8u;
    ck("ERR field n", cp_to_c_record_by_value(bad) == CP_TO_C_ERR_RECORD_N);
    bad = r; bad.q = 0;
    ck("ERR field q", cp_to_c_record_by_value(bad) == CP_TO_C_ERR_RECORD_Q);
    bad = r; bad.tag[1] = 0u;
    ck("ERR field tag", cp_to_c_record_by_value(bad) == CP_TO_C_ERR_RECORD_TAG);

    /* ---- pointer-parameter element access in a lowered class ------------ */
    vals[0] = 1.0; vals[1] = 2.0; vals[2] = 3.0; vals[3] = 4.0;
    scaled = 0.0;
    ck("PTR scale_sum rc == OK",
       cp_to_c_scale_sum(vals, 4u, 2.5, &scaled) == CP_TO_C_OK);
    ck("PTR scale_sum result == 25.0", scaled == 25.0);
    ck("ERR scale_sum n == 0 -> ERR_SIZE",
       cp_to_c_scale_sum(vals, 0u, 2.5, &scaled) == CP_TO_C_ERR_SIZE);
    ck("ERR scale_sum null p -> ERR_NULL",
       cp_to_c_scale_sum((const double *)0, 4u, 2.5, &scaled) == CP_TO_C_ERR_NULL);
    ck("ERR scale_sum null out -> ERR_NULL",
       cp_to_c_scale_sum(vals, 4u, 2.5, (double *)0) == CP_TO_C_ERR_NULL);

    /* ---- ownership contract: C+ allocates, C returns via destroy -------- */
    buf = cp_to_c_buffer_create(4u);
    ck("OWN create(4) returns non-null", buf != (void *)0);
    bsz = 0u; ecnt = 0u;
    ck("OWN buffer_meta rc == OK",
       cp_to_c_buffer_meta(buf, &bsz, &ecnt) == CP_TO_C_OK);
    ck("OWN buffer_meta byte_size == 16", bsz == 16u);
    ck("OWN buffer_meta elem_count == 4", ecnt == 4u);

    rc = CP_TO_C_OK;
    for (idx = 0u; idx < 4u; ++idx) {
        int32_t wval = (int32_t)(10u * (idx + 1u));
        int wrc = cp_to_c_buffer_write(buf, idx, wval);
        if (wrc != CP_TO_C_OK)
            rc = wrc;
    }
    ck("OWN write idx 0..3 all OK", rc == CP_TO_C_OK);

    rc = CP_TO_C_OK;
    for (idx = 0u; idx < 4u; ++idx) {
        int32_t expect = (int32_t)(10u * (idx + 1u));
        got = 0;
        int rrc = cp_to_c_buffer_read(buf, idx, &got);
        if (rrc != CP_TO_C_OK || got != expect)
            rc = 1;
    }
    ck("OWN read idx 0..3 match written values", rc == CP_TO_C_OK);
    ck("ERR write out of range -> ERR_RANGE",
       cp_to_c_buffer_write(buf, 4u, 1) == CP_TO_C_ERR_RANGE);
    ck("ERR read out of range -> ERR_RANGE",
       cp_to_c_buffer_read(buf, 9u, &got) == CP_TO_C_ERR_RANGE);
    ck("ERR read null out -> ERR_NULL",
       cp_to_c_buffer_read(buf, 0u, (int32_t *)0) == CP_TO_C_ERR_NULL);

    ck("OWN destroy rc == OK", cp_to_c_buffer_destroy(&buf) == CP_TO_C_OK);
    ck("OWN destroy clears caller handle", buf == (void *)0);
    ck("ERR destroy already-null handle -> ERR_NULL",
       cp_to_c_buffer_destroy(&buf) == CP_TO_C_ERR_NULL);
    ck("ERR meta null handle -> ERR_NULL",
       cp_to_c_buffer_meta((const void *)0, &bsz, &ecnt) == CP_TO_C_ERR_NULL);
    ck("OWN create(0) returns NULL", cp_to_c_buffer_create(0u) == (void *)0);

    /* ---- error propagation: codes round-trip unchanged ------------------ */
    ecode = CP_TO_C_ERR_FREE;
    ck("ERR echo ERR_FREE identity", cp_to_c_echo_err(ecode) == ecode);
    ck("ERR echo OK identity", cp_to_c_echo_err(CP_TO_C_OK) == CP_TO_C_OK);
    ck("ERR echo 26 identity", cp_to_c_echo_err(26) == 26);

    printf("cp_to_c: %d/%d boundary checks match\n",
           g_checks - g_failed, g_checks);
    return g_failed == 0 ? 0 : 1;
}
