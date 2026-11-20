/* Demo C: deterministic resource cleanup on scope exit and early return. */
#include <stdio.h>
#include "cplus/cplus.h"

resource struct File {
    FILE *handle;
    const char *name;
};

void File_drop(File *self) {
    if (self->handle != NULL) {
        printf("close %s\n", self->name);
        fclose(self->handle);
        self->handle = NULL;
    }
}

File File_open(const char *path, const char *name) {
    File f;
    f.handle = fopen(path, "w");
    f.name = name;
    return f;
}

int run(int early) {
    File log = File_open("/tmp/cplus_demo_c.log", "log");
    if (early) {
        printf("early return\n");
        return 1;
    }
    fputs("C+ resource demo\n", log.handle);
    printf("normal return\n");
    return 0;
}

int main(void) {
    printf("run(0)=%d\n", run(0));
    printf("run(1)=%d\n", run(1));
    return 0;
}
