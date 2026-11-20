/* Bug 14: file descriptor leak on an early error return. */
#include <stdio.h>

resource struct File {
    FILE *handle;
};

void File_drop(File *self) {
    if (self->handle != NULL) {
        printf("close\n");
        fclose(self->handle);
        self->handle = NULL;
    }
}

static int read_header(const char *path) {
    File f;
    f.handle = fopen(path, "r");
    if (f.handle == NULL)
        return -1;
    int ch = fgetc(f.handle);
    if (ch == EOF)
        return -1;
    return ch;
}

int main(void) {
    printf("%d\n", read_header("/dev/null"));
    return 0;
}
