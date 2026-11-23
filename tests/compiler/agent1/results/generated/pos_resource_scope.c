#line 1 "/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/tests/cases/resource_scope.c"
#include <stdio.h>
#include "cplus/cplus.h"

 struct File {
    FILE *handle;
    const char *label;
};
typedef struct File File;
#line 9 "/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/tests/cases/resource_scope.c"


void File_drop(File *self) {
    if (self->handle != NULL) {
        printf("drop %s\n", self->label);
        fclose(self->handle);
        self->handle = NULL;
    }
}

File File_open(const char *path, const char *label) {
    File f = {0};
    f.handle = fopen(path, "w");
    f.label = label;
    return f;
File_drop(&f); 
#line 26 "/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/tests/cases/resource_scope.c"
}

int early(int take) {
    File f = File_open("/tmp/cplus_test_early.txt", "early");
    if (take)
        { __typeof__(1) __cplus_ret = (1); File_drop(&f);  return __cplus_ret;; }
    { __typeof__(0) __cplus_ret = (0); File_drop(&f);  return __cplus_ret;; }
File_drop(&f); 
#line 33 "/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/tests/cases/resource_scope.c"
}

int main(void) {
    {
        File a = File_open("/tmp/cplus_test_a.txt", "a");
        {
            File b = File_open("/tmp/cplus_test_b.txt", "b");
            (void)b;
        File_drop(&b); 
#line 41 "/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/tests/cases/resource_scope.c"
}
        File c = File_open("/tmp/cplus_test_c.txt", "c");
        (void)c;
        (void)a;
    File_drop(&c); File_drop(&a); 
#line 45 "/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/tests/cases/resource_scope.c"
}
    printf("early=%d\n", early(1));
    printf("done\n");
    return 0;
}
