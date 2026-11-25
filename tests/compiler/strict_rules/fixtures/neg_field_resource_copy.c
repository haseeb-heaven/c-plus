/* NEGATIVE fields/containers: resource value is copied into a second owner. */
#include <stdio.h>
resource struct File { FILE *handle; };
void File_drop(File *f) { (void)f; }
static File open_file(void) { File f; f.handle = 0; return f; }
int main(void) { File a = open_file(); File b = a; (void)b; return 0; }
