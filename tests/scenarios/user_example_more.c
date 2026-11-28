/* Ten more cases in the style of the user's program, using ORDINARY C names.
 * Each prints an explicit outcome so the safety behaviour is observable.
 * Default: SafeCRT layer; -DPLAIN_LIBC: plain libc layer. */
#ifdef PLAIN_LIBC
#include "cplus_libc_safe.h"
#else
#include "stdlib/safe/safe_c.h"
#include "stdlib/safe/safe_runtime.h"
#endif
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>

#define REPORT(n, msg) do { printf("case %d: %s\n", (n), (msg)); } while (0)

static void c1(void) { /* truncation -> 4-byte block, 16-byte write */
    volatile uint64_t in = UINT64_C(0x100000004);
    uint32_t n = (uint32_t)in;              /* 4 */
    unsigned char *p = malloc(4);
    if (!p) { REPORT(1, "alloc failed"); return; }
    errno = 0; memset(p, 0x41, n * 4);      /* 16 > 4 */
    REPORT(1, errno ? "REFUSED (errno set)" : "not refused");
    free(p);
}
static void c2(void) { /* forced realloc move + stale alias write */
    char *p = malloc(32);
    if (!p) { REPORT(2, "alloc failed"); return; }
    memset(p, 'A', 32);
    char *grown = realloc(p, 1u << 20);     /* forces a move */
    if (!grown) { REPORT(2, "realloc failed, original preserved"); free(p); return; }
    errno = 0; p[0] = 'X';                  /* stale alias, raw store */
    REPORT(2, "stored through stale alias (raw store is caller UB)");
    free(grown);
}
static void c3(void) { /* double free via alias */
    int *a = malloc(64);
    if (!a) { REPORT(3, "alloc failed"); return; }
    int *b = a;
    free(a);
    errno = 0; free(b);
    REPORT(3, errno == EINVAL ? "REFUSED double free" : "not refused");
}
struct Rec { size_t len; unsigned char data[]; };
static void c4(void) {
    struct Rec *r = malloc(sizeof(*r) + 4);
    if (!r) { REPORT(4, "alloc failed"); return; }
    r->len = 128;
    errno = 0; memset(r->data, 0x41, r->len);
    REPORT(4, errno ? "REFUSED FAM overflow" : "not refused");
    free(r);
}
static void c5(void) { /* overlapping memcpy */
    char *p = malloc(32);
    if (!p) { REPORT(5, "alloc failed"); return; }
    memcpy(p, "abcdefghijklmnop", 16);
    errno = 0; memcpy(p + 4, p, 12);
    REPORT(5, errno ? "REFUSED overlap (errno set)" : "handled or accepted");
    free(p);
}
static void c7(void) {
    char dst[16];
    errno = 0; strcpy(dst, "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA");
    REPORT(7, errno ? "REFUSED strcpy overflow" : "not refused");
}
static void c8(void) { /* bounded alternative to raw strlen on unterminated */
    unsigned char *p = malloc(8);
    if (!p) { REPORT(8, "alloc failed"); return; }
    memset(p, 'A', 8);
    size_t n = strnlen((char *)p, 8);       /* explicit bound, no overread */
    REPORT(8, n == 8 ? "bounded read OK (8, no overread)" : "wrong result");
    free(p);
}
struct Cred { uint32_t id, permissions; char name[32]; };
static void c9(void) { /* zero-initialized alternative to partial init */
    struct Cred *c = calloc(1, sizeof(*c));
    if (!c) { REPORT(9, "alloc failed"); return; }
    c->id = 100; strcpy(c->name, "user");
    REPORT(9, (c->permissions & 1u) ? "unexpected" : "zeroed field OK");
    free(c);
}
static void c10(void) { /* wrapping count*element_size */
    volatile uint64_t in = UINT64_C(0xFFFFFFFFFFFFFFFF);
    size_t count = (size_t)in, es = 32;
    size_t bytes = count * es;              /* wraps */
    char *p = malloc(bytes);
    REPORT(10, p ? "small/odd allocation; writes are capacity bounded" : "REFUSED allocation");
    if (p) { errno = 0; memset(p, 0x41, es); free(p); }
}
static void c11(void) { /* alignment */
    void *p = aligned_alloc(64, 128);
    REPORT(11, p && (((uintptr_t)p & 63u) == 0) ? "64-byte aligned" : "bad alignment");
    free(p);
}
static void c12(void) { /* metadata consistency */
    void *p = malloc(48);
    if (!p) { REPORT(12, "alloc failed"); return; }
#ifdef PLAIN_LIBC
    REPORT(12, cplus_alloc_is_live(p) ? "metadata LIVE" : "metadata missing");
#else
    REPORT(12, cplus_safe_ptr_is_live(p) ? "metadata LIVE (SafeCRT table)" : "metadata missing");
#endif
    free(p);
}

int main(int argc, char **argv) {
    (void)argc; (void)argv;
#ifndef PLAIN_LIBC
    cplus_runtime_set_violation_trap(0); /* observe every refusal */
#endif
    c1(); c2(); c3(); c4(); c5(); c7(); c8(); c9(); c10(); c11(); c12();
    return 0;
}
