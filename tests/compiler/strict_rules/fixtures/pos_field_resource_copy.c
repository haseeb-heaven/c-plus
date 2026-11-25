/* POSITIVE fields/containers: resource is accessed through its owner, never copied by value. */
#include <stdio.h>
resource struct File { FILE *handle; };
void File_drop(File *f) { if (f->handle) { fclose(f->handle); f->handle = 0; } }
static void use(File *f) { (void)f; }
int main(void) { File f = {0}; use(&f); return 0; }
