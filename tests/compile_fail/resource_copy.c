#include <stdio.h>

resource struct File { FILE *handle; };
void File_drop(File *self) { (void)self; }
File File_open(void) { File f; f.handle = NULL; return f; }

int main(void) {
    File a = File_open();
    File b = a;
    (void)b;
    return 0;
}
