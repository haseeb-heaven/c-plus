/* Standalone reproducers for the RED20 safecrt findings.
 * Build via run.sh, then:
 *   ./repro null      -- cplus_memcmp_s_auto(NULL,NULL,0) reports "equal"
 *   ./repro oob       -- memset_s with dmax=cap+1 corrupts canary, returns 0
 *   ./repro unterm    -- strnlen_s with legal smax=4096 over-reads -> SIGBUS
 */
#define _DARWIN_C_SOURCE
#define _POSIX_C_SOURCE 200112L
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
typedef int errno_t; typedef size_t rsize_t;
extern errno_t cplus_memset_s(void *, rsize_t, int, rsize_t);
extern int cplus_memcmp_s_auto(const void *, const void *, size_t, size_t, size_t);
extern rsize_t cplus_strnlen_s(const char *, rsize_t);
extern void cplus_runtime_set_violation_trap(int);
extern void cplus_sc_set_mem_constraint_handler_s_wrapper(void (*)(const char *, void *, int));
extern void cplus_sc_set_str_constraint_handler_s_wrapper(void (*)(const char *, void *, int));

static void noop(const char *m, void *p, int e) { (void)m; (void)p; (void)e; }
static size_t ps;
/* 64 live bytes, then 256 canary bytes, then a PROT_NONE guard page. */
static unsigned char *mk(unsigned char **base) {
    unsigned char *b = mmap(NULL, 2 * ps, PROT_NONE, MAP_PRIVATE | MAP_ANON, -1, 0);
    if (b == MAP_FAILED) return NULL;
    if (mprotect(b, ps, PROT_READ | PROT_WRITE) != 0) return NULL;
    memset(b, 0xC3, ps);
    *base = b;
    return b + ps - 256 - 64;
}
static int canary_ok(const unsigned char *d) {
    for (int i = 0; i < 256; ++i) if (d[64 + i] != 0xC3) return 0;
    return 1;
}
int main(int argc, char **argv) {
    const char *mode = argc > 1 ? argv[1] : "null";
    ps = (size_t)sysconf(_SC_PAGESIZE);
    cplus_runtime_set_violation_trap(0);
    cplus_sc_set_mem_constraint_handler_s_wrapper(noop);
    cplus_sc_set_str_constraint_handler_s_wrapper(noop);

    if (!strcmp(mode, "null")) {
        char b[8] = {0};
        errno = 0;
        printf("memcmp_auto(NULL,NULL,n=0) = %d (errno=%d)\n",
               cplus_memcmp_s_auto(NULL, NULL, 0, (size_t)-1, (size_t)-1), errno);
        errno = 0;
        printf("memcmp_auto(buf,NULL,n=0)  = %d (errno=%d)\n",
               cplus_memcmp_s_auto(b, NULL, 0, sizeof b, (size_t)-1), errno);
        printf("EXPECTED: non-zero refusal (raw cplus_memcmp_s returns 400 ESNULLP)\n");
    } else if (!strcmp(mode, "oob")) {
        unsigned char *base; unsigned char *d = mk(&base);
        errno_t rc = cplus_memset_s(d, /*dmax=*/65, /*value=*/0x41, /*n=*/65);
        printf("memset_s(dmax=65,n=65, real cap=64) rc=%d canary_intact=%d first_bad_byte=%02x\n",
               rc, canary_ok(d), d[64]);
        printf("EXPECTED: refusal and intact canary; OBSERVED: success + byte 64 clobbered\n");
    } else if (!strcmp(mode, "unterm")) {
        unsigned char *base; unsigned char *s = mk(&base);
        memset(s, 'A', 64);              /* no NUL before the guard page */
        printf("strnlen_s(unterminated, smax=4096) = %zu\n",
               cplus_strnlen_s((const char *)s, 4096));
        printf("EXPECTED: defined refusal; OBSERVED: scan crosses PROT_NONE page\n");
    } else {
        fprintf(stderr, "usage: %s null|oob|unterm\n", argv[0]);
        return 2;
    }
    return 0;
}
