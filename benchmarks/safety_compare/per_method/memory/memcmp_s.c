/* Checked memcmp_s cases. One harness == one subprocess == one method. */
#include "mem_harness.h"

int main(void) {
    const char *method = "memcmp_s";
    unsigned char buf[MEM_TOTAL];
    unsigned char src[MEM_N];
    int diff = -99;
    sc_errno_t rc;
    int guards_ok;
    int i;

    cplus_sc_set_mem_constraint_handler_s_wrapper(mem_noop_handler);
    for (i = 0; i < MEM_N; ++i) {
        src[i] = (unsigned char)(i + 1);
    }

    /* 1) valid exact-fit comparison. */
    mem_init_guarded(buf, MEM_TOTAL, MEM_N);
    rc = cplus_sc_memcmp_s(buf, MEM_N, src, MEM_N, &diff);
    guards_ok = mem_guards_intact(buf, MEM_TOTAL, MEM_N);
    mem_emit(method, "valid_exact_fit", rc, guards_ok);

    /* 2) null source with a valid, guarded destination operand (ESNULLP). */
    mem_init_guarded(buf, MEM_TOTAL, MEM_N);
    rc = cplus_sc_memcmp_s(buf, MEM_N, NULL, MEM_N, &diff);
    guards_ok = mem_guards_intact(buf, MEM_TOTAL, MEM_N);
    mem_emit(method, "null_src", rc, guards_ok);

    /* 3) destination operand one byte too small (ESNOSPC). */
    mem_init_guarded(buf, MEM_TOTAL, MEM_N - 1);
    rc = cplus_sc_memcmp_s(buf, MEM_N - 1, src, MEM_N, &diff);
    guards_ok = mem_guards_intact(buf, MEM_TOTAL, MEM_N - 1);
    mem_emit(method, "dest_one_byte_too_small", rc, guards_ok);

    /* 4) SIZE_MAX length: slen > RSIZE_MAX_MEM (ESLEMAX). */
    mem_init_guarded(buf, MEM_TOTAL, MEM_N);
    rc = cplus_sc_memcmp_s(buf, MEM_N, src, (sc_rsize_t)-1, &diff);
    guards_ok = mem_guards_intact(buf, MEM_TOTAL, MEM_N);
    mem_emit(method, "size_max_length", rc, guards_ok);

    /* 5) overlapping operands: memcmp_s has no overlap constraint (EOK). */
    mem_init_guarded(buf, MEM_TOTAL, MEM_N);
    rc = cplus_sc_memcmp_s(buf, MEM_N, buf + 1, MEM_N - 1, &diff);
    guards_ok = mem_guards_intact(buf, MEM_TOTAL, MEM_N);
    mem_emit(method, "overlap", rc, guards_ok);

    return 0;
}
