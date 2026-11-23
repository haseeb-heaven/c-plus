#line 1 "/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/tests/cases/classic_bug14_fd_leak.c"
/* Bug 14: file descriptor leak on an early error return. */
#include <stdio.h>

 struct File {
    FILE *handle;
};
typedef struct File File;
#line 8 "/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/tests/cases/classic_bug14_fd_leak.c"


void File_drop(File *self) {
    if (self->handle != NULL) {
        printf("close\n");
        fclose(self->handle);
        self->handle = NULL;
    }
}

static int read_header(const char *path) {
    File f = {0};
    f.handle = fopen(path, "r");
    if (f.handle == NULL)
        { __typeof__(- 1) __cplus_ret = (- 1); File_drop(&f);  return __cplus_ret;; }
    int ch = fgetc(f.handle);
    if (ch == EOF)
        { __typeof__(- 1) __cplus_ret = (- 1); File_drop(&f);  return __cplus_ret;; }
    { __typeof__(ch) __cplus_ret = (ch); File_drop(&f);  return __cplus_ret;; }
File_drop(&f); 
#line 29 "/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/tests/cases/classic_bug14_fd_leak.c"
}

int main(void) {
    printf("%d\n", read_header("/dev/null"));
    return 0;
}
