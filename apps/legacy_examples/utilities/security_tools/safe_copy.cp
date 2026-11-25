/* safe_copy.cp -- deterministic DEMONSTRATION of checked copy refusals.
 *
 * NOT a production file-copy utility: it copies fixed in-memory test bytes,
 * never touches files, and argv only selects the case.  One hostile case does
 * an unguarded raw byte copy in a forked child (private address space) so the
 * "before" corruption can never touch the parent's canary; every other case
 * uses the checked cplus_memcpy_s API in this process.
 *
 * Build (from repo root):
 *   ./build/c+ build examples/utilities/security_tools/safe_copy.cp \
 *     --strict --sanitize=none -I. -o examples/utilities/security_tools/safe_copy \
 *     -- build/safe/libcplus_safecrt.a build/libc/libcplus_hardened.a
 *
 * Usage:
 *   safe_copy exact | over | nullsrc | null | raw
 *
 *   exact    cplus_memcpy_s(dst, 8, src8, 8)  -> 0    (accepted)
 *   over     cplus_memcpy_s(dst, 8, src9, 9)  -> 406  (ESNOSPC, REFUSED)
 *   nullsrc  cplus_memcpy_s(dst, 8, NULL, 8)  -> 400  (ESNULLP, REFUSED)
 *   null     cplus_memcpy_s(NULL, 8, src8, 8) -> 400  (ESNULLP, REFUSED)
 *   raw      unguarded 9-byte memcpy in a child -> child canary corrupted
 */
#define CPLUS_SAFE_NO_UNPREFIXED_MACROS 1
#include "stdlib/safe/safe_error.h"
#include "stdlib/safe/safe_memory.h"

#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include "cplus/cplus.h"
#include "stdlib/error.hp"

#define SC_DST_CAP 8
#define SC_CANARY  0xa5u
#define SC_HIT     0x2au

struct sc_guarded {
    unsigned char buf[SC_DST_CAP];
    volatile unsigned char canary;
};

static void sc_arg(char **argv, int i, const char **out) {
    const char *v = (const char *)0;
    __builtin_memcpy(&v, argv + i, sizeof(v));
    *out = v;
}

/* Refusal lines carry the real safeclib constraint code returned by the API. */
static int refuse(const char *label, int code, const char *reason,
                  unsigned char canary) {
    printf("SAFECOPY %s REFUSED code=%d reason=%s canary=0x%02x intact=%d\n",
           label, code, reason, (unsigned)canary, canary == SC_CANARY);
    fflush(stdout);
    return 2;
}

static void sc_init(struct sc_guarded *g) {
    size_t i;
    for (i = 0; i < (size_t)SC_DST_CAP; i++)
        g->buf[i] = 0;
    g->canary = SC_CANARY;
}

/* The unguarded "before" case, confined to a child process. */
static int sc_raw_case(void) {
    unsigned char src9[9] = {1, 2, 3, 4, 5, 6, 7, 8, SC_HIT};
    struct sc_guarded child;
    pid_t pid;
    int status = 0;
    int child_code = -1;
    int corrupted = 0;

    sc_init(&child);
    pid = fork();
    if (pid == 0) {
        volatile unsigned char hit;
        __builtin_memcpy((void *)child.buf, (const void *)src9, 9u);
        hit = child.canary;
        printf("SAFECOPY raw OK canary=0x%02x corrupted=%d (unguarded subprocess)\n",
               (unsigned)hit, hit == SC_HIT);
        fflush(stdout);
        _exit(hit == SC_HIT ? 9 : 8);
    }
    if (pid > 0) {
        (void)waitpid(pid, &status, 0);
        if (WIFEXITED(status))
            child_code = WEXITSTATUS(status);
        else if (WIFSIGNALED(status))
            child_code = 100 + WTERMSIG(status);
        corrupted = (child_code == 9);
    }
    printf("SAFECOPY raw_child %s child_exit=%d parent_canary=0x%02x\n",
           corrupted ? "OK" : "REFUSED", child_code,
           (unsigned)SC_CANARY);
    fflush(stdout);
    return corrupted ? 0 : 1;
}

int main(int argc, char **argv) {
    unsigned char src9[9] = {1, 2, 3, 4, 5, 6, 7, 8, SC_HIT};
    unsigned char src8[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    struct sc_guarded dst;
    const char *which = (const char *)0;
    errno_t rc;
    int failed = 0;

    if (argc < 2)
        return refuse("usage", 108, "missing-case", SC_CANARY);
    if (argc > 2)
        return refuse("usage", 108, "too-many-args", SC_CANARY);
    sc_arg(argv, 1, &which);
    if (which == (const char *)0)
        return refuse("usage", 108, "null-arg", SC_CANARY);

    if (strcmp(which, "raw") == 0)
        return sc_raw_case();

    sc_init(&dst);
    if (strcmp(which, "over") == 0) {
        rc = cplus_memcpy_s((void *)dst.buf, (rsize_t)SC_DST_CAP,
                            (const void *)src9, (rsize_t)sizeof(src9));
        if (rc == CPLUS_SAFE_OK || dst.canary != SC_CANARY)
            return refuse("over", (int)rc, "canary-corrupted-or-accepted",
                          dst.canary);
        return refuse("over", (int)rc, "slen-exceeds-dmax", dst.canary);
    }
    if (strcmp(which, "nullsrc") == 0) {
        rc = cplus_memcpy_s((void *)dst.buf, (rsize_t)SC_DST_CAP,
                            (const void *)0, (rsize_t)SC_DST_CAP);
        if (rc == CPLUS_SAFE_OK)
            return refuse("nullsrc", (int)rc, "accepted-null-source", dst.canary);
        return refuse("nullsrc", (int)rc, "null-source", dst.canary);
    }
    if (strcmp(which, "null") == 0) {
        rc = cplus_memcpy_s((void *)0, (rsize_t)SC_DST_CAP,
                            (const void *)src8, (rsize_t)sizeof(src8));
        if (rc == CPLUS_SAFE_OK)
            return refuse("null", (int)rc, "accepted-null-destination",
                          dst.canary);
        return refuse("null", (int)rc, "null-destination", dst.canary);
    }
    if (strcmp(which, "exact") == 0) {
        rc = cplus_memcpy_s((void *)dst.buf, (rsize_t)SC_DST_CAP,
                            (const void *)src8, (rsize_t)sizeof(src8));
        if (rc != CPLUS_SAFE_OK || dst.canary != SC_CANARY ||
            dst.buf[SC_DST_CAP - 1] != 8u) {
            printf("SAFECOPY exact REFUSED code=%d canary=0x%02x\n",
                   (int)rc, (unsigned)dst.canary);
            fflush(stdout);
            return 2;
        }
        printf("SAFECOPY exact OK rc=0 canary=0x%02x buf7=%d\n",
               (unsigned)dst.canary, (int)dst.buf[SC_DST_CAP - 1]);
        fflush(stdout);
        return 0;
    }

    printf("SAFECOPY unknown_case REFUSED code=108 reason=unknown-case\n");
    fflush(stdout);
    failed = 1;
    return failed;
}
