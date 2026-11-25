// scenario: p0new_file_alias_write_uaf
// EXPECT: reject
// DIAG: use of closed FILE*
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
struct p0new_io { int tag; FILE *stream; };
void p0new_file_alias_write_uaf(void){
    struct p0new_io meta = { .tag = 7, .stream = 0 };
    FILE *primary = fopen("p0new.log", "a+");
    if (!primary) return;
    meta.stream = primary;
    FILE *mirror = primary;
    fclose(primary);
    fputc('x', mirror);
    (void)meta.tag;
}
