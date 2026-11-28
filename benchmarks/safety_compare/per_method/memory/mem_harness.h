/* Shared helpers for the per-method memory checked-API harnesses.
 *
 * Scope: deterministic stdout lines of the form "<method> <case> <token>".
 * Diagnostics (exact safeclib return codes, canary state) go to stderr so the
 * compare driver can diff tokens without noise while NOTES.md keeps evidence.
 *
 * Canaries are guard bytes placed immediately AFTER the declared destination
 * capacity (dmax). safeclib deliberately zeroes up to dmax bytes of dest on
 * several constraint violations; that is inside the declared region, not a
 * canary violation. Guard bytes outside dmax must survive every refusal.
 */
#ifndef MEM_HARNESS_H
#define MEM_HARNESS_H

#include <stddef.h>
#include <stdio.h>
#include <string.h>

typedef int sc_errno_t;
typedef size_t sc_rsize_t;

enum { MEM_N = 8, MEM_G = 8, MEM_TOTAL = MEM_N + MEM_G };
#define MEM_GUARD_BYTE 0xA5u

/* cplus_sc_* are the safeclib-backed checked APIs in libcplus_safecrt.a. */
extern sc_errno_t cplus_sc_memcpy_s(void *dest, sc_rsize_t dmax,
                                    const void *src, sc_rsize_t smax);
extern sc_errno_t cplus_sc_memmove_s(void *dest, sc_rsize_t dmax,
                                     const void *src, sc_rsize_t smax);
extern sc_errno_t cplus_sc_memset_s(void *dest, sc_rsize_t dmax, int value,
                                    sc_rsize_t n);
extern sc_errno_t cplus_sc_memcmp_s(const void *dest, sc_rsize_t dmax,
                                    const void *src, sc_rsize_t smax,
                                    int *diff);

/* Silence the safeclib constraint handler so a refusal stays an observable
 * return code instead of an abort/stderr banner. */
extern void cplus_sc_set_mem_constraint_handler_s_wrapper(
    void (*handler)(const char *, void *, int));

static void mem_noop_handler(const char *msg, void *ptr, int error) {
    (void)msg;
    (void)ptr;
    (void)error;
}

static void mem_init_guarded(unsigned char *buf, size_t total,
                             size_t guard_start) {
    size_t i;
    for (i = 0; i < guard_start; ++i) {
        buf[i] = 0x11u;
    }
    for (; i < total; ++i) {
        buf[i] = MEM_GUARD_BYTE;
    }
}

static int mem_guards_intact(const unsigned char *buf, size_t total,
                             size_t guard_start) {
    size_t i;
    for (i = guard_start; i < total; ++i) {
        if (buf[i] != MEM_GUARD_BYTE) {
            return 0;
        }
    }
    return 1;
}

static const char *mem_token(int rc, int guards_ok) {
    if (!guards_ok) {
        return "CANARY_BROKEN";
    }
    return rc == 0 ? "OK" : "REFUSED";
}

static void mem_emit(const char *method, const char *case_name, int rc,
                     int guards_ok) {
    fprintf(stderr, "DIAG %s %s rc=%d guards=%d\n", method, case_name, rc,
            guards_ok);
    printf("%s %s %s\n", method, case_name, mem_token(rc, guards_ok));
}

static void mem_emit_na(const char *method, const char *case_name,
                        const char *reason) {
    fprintf(stderr, "DIAG %s %s N/A %s\n", method, case_name, reason);
    printf("%s %s N/A\n", method, case_name);
}

#endif /* MEM_HARNESS_H */
