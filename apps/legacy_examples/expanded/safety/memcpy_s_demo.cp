/* Safety demo 1: ordinary (raw C) memcpy corrupts a canary; the additive
 * checked cplus_memcpy_s REFUSES the same one-over write.
 *
 * The UNGUARDED write is executed in a forked child process (a real
 * subprocess with its own address space), so a corruption can never touch the
 * demo process's own canary.  The parent records the child's exit status and
 * then runs the checked call locally.  Nothing here is a compile-time check:
 * both outcomes are observed at run time.
 *
 * Build (from repo root):
 *   ./build/c+ build examples/expanded/safety/memcpy_s_demo.cp \
 *     --strict --sanitize=none -I. -o examples/expanded/safety/memcpy_s_demo \
 *     -- build/safe/libcplus_safecrt.a build/libc/libcplus_hardened.a
 *
 * Output: deterministic "memcpy_s <case> OK|REFUSED <detail>" lines.
 * Exit 0 iff the child corrupted its canary, the checked one-over was refused
 * with the parent canary intact, and the checked exact-fit copy succeeded.
 */
#define CPLUS_SAFE_NO_UNPREFIXED_MACROS 1
#include "stdlib/safe/safe_memory.h"

#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#define CANARY_OK   0xA5u
#define CANARY_HIT  0x2Au  /* the 9th source byte that lands on the canary */

/* The canary is volatile so the child's read cannot be optimized away. */
struct guarded {
    unsigned char buf[8];
    volatile unsigned char canary;
};

static int g_failed = 0;

/* mark(case, got, want, detail): emit a line and remember a mismatch. */
static void mark(const char *case_name, const char *got, const char *want,
                 const char *detail) {
    printf("memcpy_s %s %s %s\n", case_name, got, detail);
    if (strcmp(got, want) != 0)
        g_failed = 1;
    fflush(stdout);
}

int main(void) {
    unsigned char src9[9] = {1, 2, 3, 4, 5, 6, 7, 8, CANARY_HIT};
    unsigned char src8[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    struct guarded child;
    struct guarded parent;
    pid_t pid;
    int status = 0;
    int child_code = -1;
    char detail[128];
    errno_t rc;

    /* ---- BEFORE: unguarded raw memcpy in a subprocess -------------------- */
    /* fork() gives the child a private (copy-on-write) address space; the
     * one-over write below can only corrupt the child's canary. */
    child.buf[0] = 0; child.buf[1] = 0; child.buf[2] = 0; child.buf[3] = 0;
    child.buf[4] = 0; child.buf[5] = 0; child.buf[6] = 0; child.buf[7] = 0;
    child.canary = CANARY_OK;

    pid = fork();
    if (pid == 0) {
        int corrupted;
        /* Raw C memcpy semantics: no destination-capacity argument and no
         * check.  __builtin_memcpy is the compiler's unchecked byte copy. */
        __builtin_memcpy((void *)child.buf, (const void *)src9, 9u);
        corrupted = (child.canary == CANARY_HIT);
        /* The child speaks the same protocol; "OK" here means the unguarded
         * call was accepted (no refusal) and did corrupt the byte. */
        snprintf(detail, sizeof(detail),
                 "canary=0x%02x corrupted=%d (unguarded subprocess)",
                 (unsigned)child.canary, corrupted);
        printf("memcpy_s ordinary_one_over %s %s\n",
               corrupted ? "OK" : "REFUSED", detail);
        fflush(stdout);
        _exit(corrupted ? 9 : 8);
    }

    /* Parent: wait for the child and record its exit code. */
    if (pid > 0) {
        (void)waitpid(pid, &status, 0);
        if (WIFEXITED(status))
            child_code = WEXITSTATUS(status);
        else if (WIFSIGNALED(status))
            child_code = 100 + WTERMSIG(status);
        snprintf(detail, sizeof(detail), "child_exit=%d", child_code);
        mark("child_exit", child_code == 9 ? "OK" : "REFUSED",
             "OK", detail);
    } else {
        mark("child_exit", "REFUSED", "OK", "fork failed");
    }

    /* ---- AFTER: checked cplus_memcpy_s in the demo process --------------- */
    /* One-over: dmax=8, smax=9 -> ESNOSPC (406), no destination write. */
    parent.buf[0] = 0; parent.buf[1] = 0; parent.buf[2] = 0; parent.buf[3] = 0;
    parent.buf[4] = 0; parent.buf[5] = 0; parent.buf[6] = 0; parent.buf[7] = 0;
    parent.canary = CANARY_OK;
    rc = cplus_memcpy_s((void *)parent.buf, 8u, (const void *)src9, 9u);
    snprintf(detail, sizeof(detail), "rc=%d canary=0x%02x intact=%d",
             (int)rc, (unsigned)parent.canary,
             parent.canary == CANARY_OK);
    mark("checked_one_over", rc != 0 && parent.canary == CANARY_OK
             ? "REFUSED" : "OK",
         "REFUSED", detail);

    /* Positive control: an exact fit is accepted and copies all 8 bytes. */
    rc = cplus_memcpy_s((void *)parent.buf, 8u, (const void *)src8, 8u);
    snprintf(detail, sizeof(detail), "rc=%d canary=0x%02x buf7=%d",
             (int)rc, (unsigned)parent.canary, (int)parent.buf[7]);
    mark("checked_exact_fit", rc == 0 && parent.canary == CANARY_OK &&
             parent.buf[7] == 8 ? "OK" : "REFUSED", "OK", detail);

    /* Final guard: the demo process's canary was never corrupted. */
    snprintf(detail, sizeof(detail), "canary=0x%02x", (unsigned)parent.canary);
    mark("parent_canary", parent.canary == CANARY_OK ? "OK" : "REFUSED",
         "OK", detail);

    return g_failed;
}
