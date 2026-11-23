#include <stdio.h>
#include <stdint.h>
#include <time.h>
#include "cplus/cplus.h"

int main(void) {
    char line[128];
    FILE *f;
    struct timespec ts;
    int a=0,b=0,c=0;
    f = fopen("tests/fuzz10k/containers/results/_io_probe_7.log", "r");
    if (f == NULL) { print("NOFILE"); return 1; }
    while (fgets(line, (int)sizeof(line), f) != NULL) {
        if (sscanf(line, "%d %d %d", &a, &b, &c) == 3) {
            print("PARSED", a, b, c);
        } else {
            print("RAW", line);
        }
    }
    fclose(f);
    ts.tv_sec = 0; ts.tv_nsec = 1000000;
    nanosleep(&ts, NULL);
    print("SLEPT");
    return 0;
}
