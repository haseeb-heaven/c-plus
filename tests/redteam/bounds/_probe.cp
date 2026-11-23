#include <stdio.h>
#include <unistd.h>
#include <sys/wait.h>
#include <stdlib.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/memory.hp"
#include "stdlib/lifetime_adapt.hp"
#include "stdlib/allocator.hp"

int main(void) {
    error e = error();
    void *p = stdlib_alloc(16);
    printf("probe p=%p cap=%zu gen=%llu\n", p, stdlib_mem_capacity(p), stdlib_mem_token(p));
    printf("span_ok=%d query=%d\n", stdlib_mem_span_ok(p, 16, 4, 4), stdlib_mem_query(p, 4, 4, stdlib_mem_token(p), &e));
    fflush(stdout);
    pid_t pid = fork();
    if (pid == 0) {
        _exit(0);
    }
    int st = 0;
    waitpid(pid, &st, 0);
    printf("child status=%d\n", st);
    stdlib_mem_release(&p);
    return 0;
}
