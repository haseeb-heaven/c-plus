/* C5 regression: huge-but-non-overflowing allocation requests must be refused
 * recoverably (NULL/STDLIB_FAIL + error*) instead of reaching cplus_alloc,
 * whose malloc failure calls cplus_print_fatal and exits the process.
 *
 * Each call runs in a forked child: a fatal exit (CPLUS_EXIT_RESOURCE == 107)
 * is observed as a non-zero child status and fails the case deterministically.
 */
#include <stdint.h>
#include <unistd.h>
#include <sys/wait.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/datastack.hp"
#include "stdlib/arena.hp"

/* 1 when the child returned cleanly with the documented refusal contract. */
static int child_refused(int (*fn)(void *), void *arg) {
    pid_t pid = fork();
    int st = 0;
    if (pid == 0) {
        /* Each fn returns 0 only when it observed the documented refusal. */
        _exit(fn(arg) == 0 ? 0 : 1);
    }
    if (pid < 0) return 0;
    if (waitpid(pid, &st, 0) != pid) return 0;
    return WIFEXITED(st) && WEXITSTATUS(st) == 0;
}

static int ds_init_huge(void *unused) {
    error e = error();
    stdlib_datastack ds;
    int rc;
    (void)unused;
    rc = stdlib_datastack_init(&ds, SIZE_MAX / 4u, &e);
    if (rc != STDLIB_FAIL) return 1;
    if (e.code() != (int)CPLUS_EXIT_BOUNDS) return 1;
    if (e.category() != STDLIB_CATEGORY_RANGE) return 1;
    return 0;
}

static int arena_init_huge(void *unused) {
    error e = error();
    stdlib_arena a;
    int rc;
    (void)unused;
    rc = stdlib_arena_init(&a, SIZE_MAX / 4u, &e);
    if (rc != STDLIB_FAIL) return 1;
    if (e.code() != (int)CPLUS_EXIT_BOUNDS) return 1;
    if (e.category() != STDLIB_CATEGORY_RANGE) return 1;
    return 0;
}

static int arena_spill_huge(void *unused) {
    error e = error();
    stdlib_arena a;
    void *p;
    (void)unused;
    if (stdlib_arena_init(&a, 1024u, &e) != STDLIB_OK) return 1;
    p = stdlib_arena_alloc(&a, SIZE_MAX / 4u, 16u, &e);
    if (p != (void *)0) return 1;
    if (e.code() != (int)CPLUS_EXIT_BOUNDS) return 1;
    if (e.category() != STDLIB_CATEGORY_RANGE) return 1;
    stdlib_arena_destroy(&a, &e);
    return 0;
}

int main(void) {
    int fails = 0;
    if (!child_refused(ds_init_huge, (void *)0)) fails++;
    if (!child_refused(arena_init_huge, (void *)0)) fails++;
    if (!child_refused(arena_spill_huge, (void *)0)) fails++;
    print("C5_ALLOC_CEILING", fails);
    return fails ? 1 : 0;
}
