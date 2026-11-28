#include <errno.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/json.hp"
#include "stdlib/log.hp"

int main(void) {
    pid_t pid;
    int status = 0;
    struct rusage ru;
    error e = error();
    json d = json_parse_budget("{\"a\":1}", 7, 8, &e);
    unsigned char guard[16];
    __builtin_memset(guard, 0xA5, 16);
    pid = fork();
    if (pid == 0) {
        alarm(2);
        json x = json_parse_budget("{\"b\":2}", 7, 8, &e);
        unsigned char g2[16];
        __builtin_memset(g2, 0x5A, 16);
        x.drop();
        _exit((g2[0] == 0x5A && x.valid() == 0) ? 3 : 4);
    }
    if (pid > 0) {
        wait4(pid, &status, 0, &ru);
        printf("child=%d exited=%d signal=%d rss=%ld dvalid=%d guard=%d\n",
               (int)pid, WIFEXITED(status), WIFSIGNALED(status),
               (long)ru.ru_maxrss, d.valid(), guard[0] == 0xA5);
    }
    d.drop();
    return 0;
}
