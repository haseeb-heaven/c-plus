/* Checked memset_s cases. One harness == one subprocess == one method. */
#include "mem_harness.h"

int main(void) {
    const char *method = "memset_s";
    unsigned char buf[MEM_TOTAL];
    sc_errno_t rc;
    int guards_ok;

    cplus_sc_set_mem_constraint_handler_s_wrapper(mem_noop_handler);

    /* 1) valid exact-fit. */
    mem_init_guarded(buf, MEM_TOTAL, MEM_N);
    rc = cplus_sc_memset_s(buf, MEM_N, 0x5Au, MEM_N);
    guards_ok = mem_guards_intact(buf, MEM_TOTAL, MEM_N);
    mem_emit(method, "valid_exact_fit", rc, guards_ok);

    /* 2) null destination (ESNULLP; no memory touched). */
    mem_init_guarded(buf, MEM_TOTAL, MEM_N);
    rc = cplus_sc_memset_s(NULL, MEM_N, 0x5Au, MEM_N);
    guards_ok = mem_guards_intact(buf, MEM_TOTAL, MEM_N);
    mem_emit(method, "null_dest", rc, guards_ok);

    /* 3) destination one byte too small: dmax = N-1, n = N (ESNOSPC). */
    mem_init_guarded(buf, MEM_TOTAL, MEM_N - 1);
    rc = cplus_sc_memset_s(buf, MEM_N - 1, 0x5Au, MEM_N);
    guards_ok = mem_guards_intact(buf, MEM_TOTAL, MEM_N - 1);
    mem_emit(method, "dest_one_byte_too_small", rc, guards_ok);

    /* 4) SIZE_MAX length: n > RSIZE_MAX_MEM (ESLEMAX). */
    mem_init_guarded(buf, MEM_TOTAL, MEM_N);
    rc = cplus_sc_memset_s(buf, MEM_N, 0x5Au, (sc_rsize_t)-1);
    guards_ok = mem_guards_intact(buf, MEM_TOTAL, MEM_N);
    mem_emit(method, "size_max_length", rc, guards_ok);

    /* 5) no source/dest overlap concept for a single-pointer fill. */
    mem_emit_na(method, "overlap", "single destination pointer; no source operand");

    return 0;
}
