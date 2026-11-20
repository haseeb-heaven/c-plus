#include <stdio.h>
#include "cplus/cplus.h"

resource struct File {
    FILE *handle;
    const char *label;
};

void File_drop(File *self) {
    if (self->handle != NULL) {
        printf("drop %s\n", self->label);
        fclose(self->handle);
        self->handle = NULL;
    }
}

File File_open(const char *path, const char *label) {
    File f;
    f.handle = fopen(path, "w");
    f.label = label;
    return f;
}

int early(int take) {
    File f = File_open("/tmp/cplus_test_early.txt", "early");
    if (take)
        return 1;
    return 0;
}

int main(void) {
    {
        File a = File_open("/tmp/cplus_test_a.txt", "a");
        {
            File b = File_open("/tmp/cplus_test_b.txt", "b");
            (void)b;
        }
        File c = File_open("/tmp/cplus_test_c.txt", "c");
        (void)c;
        (void)a;
    }
    printf("early=%d\n", early(1));
    printf("done\n");
    return 0;
}
